/**
 * bench_orderbook.cpp
 *
 * Benchmarks OrderBook throughput using the LOBSTER sample data.
 *
 * Key design choices:
 *
 *  1. Pre-parse CSV once — string/IO cost stays outside timed loops.
 *
 *  2. Per-run price jitter — each run gets a fresh copy of the event
 *     sequence with every price independently randomised by ±JITTER_PCT.
 *     Without this, the CPU branch predictor learns the exact pattern of
 *     matches/misses from run 1 and all subsequent runs are artificially
 *     fast.  Real market data is never the same sequence twice.
 *
 *  3. Warmup uses jittered data too — if warmup used the raw sequence,
 *     it would pre-teach the predictor the benchmark pattern, defeating
 *     the purpose of the jitter.
 *
 *  4. Perturb step is NOT timed — only the actual order-book processing
 *     is inside the clock window.
 *
 *  5. stdout suppressed via RAII — OrderBook prints every trade; that
 *     I/O cost would dwarf the actual measurement.
 *
 * Compile (from project root):
 *   g++ -std=c++20 -O2 -o bench_orderbook bench_orderbook.cpp
 *
 * Run:
 *   ./bench_orderbook
 *   ./bench_orderbook path/to/other_data.csv
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <chrono>
#include <random>
#include <unordered_map>
#include "orderBook.h"


// ─────────────────────────────────────────────────────────────────────────────
//  Config
// ─────────────────────────────────────────────────────────────────────────────

static constexpr int    WARMUP_RUNS    = 10;
static constexpr int    BENCHMARK_RUNS = 100;

// Each order's price is multiplied by a uniform random factor in
// [1 - JITTER_PCT, 1 + JITTER_PCT] before every run.
// 2% is enough to meaningfully change matching behaviour (different orders
// cross, different fill quantities, different loop trip counts) without
// making the synthetic data wildly unlike real AAPL tick data.
static constexpr double JITTER_PCT     = 0.02;

static const std::string DATA_FILE =
    "SampleData/AAPL_2012-06-21_34200000_57600000_message_1.csv";


// ─────────────────────────────────────────────────────────────────────────────
//  NullStreambuf / SuppressStdout
// ─────────────────────────────────────────────────────────────────────────────

struct NullStreambuf : std::streambuf {
    int             overflow(int c)                        override { return c; }
    std::streamsize xsputn(const char*, std::streamsize n) override { return n; }
};

// RAII: mutes std::cout on construction, restores it on destruction.
struct SuppressStdout {
    NullStreambuf   nullBuf;
    std::streambuf* saved;
    SuppressStdout()  : saved(std::cout.rdbuf(&nullBuf)) {}
    ~SuppressStdout() { std::cout.rdbuf(saved); }
};


// ─────────────────────────────────────────────────────────────────────────────
//  Event — one parsed CSV row
// ─────────────────────────────────────────────────────────────────────────────

struct Event {
    int      type;     // 1 = add, 3 = cancel  (2 = modify skipped)
    int      theirId;
    Quantity quantity;
    Price    price;
    Side     side;     // meaningful only for type == 1
};


// ─────────────────────────────────────────────────────────────────────────────
//  loadEvents — parse CSV into typed events once
// ─────────────────────────────────────────────────────────────────────────────

std::vector<Event> loadEvents(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open())
        throw std::runtime_error("Cannot open: " + filename);

    std::vector<Event> events;
    std::string line;
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string tok;
        std::vector<std::string> f;
        while (std::getline(ss, tok, ',')) f.push_back(tok);
        if (f.size() < 6) continue;

        int type = std::stoi(f[1]);
        if (type != 1 && type != 3) continue;

        Event e;
        e.type     = type;
        e.theirId  = std::stoi(f[2]);
        e.quantity = static_cast<Quantity>(std::stoi(f[3]));
        e.price    = static_cast<Price>(std::stoi(f[4]));
        e.side     = (std::stoi(f[5]) == 1) ? Side::Bid : Side::Ask;
        events.push_back(e);
    }
    return events;
}


// ─────────────────────────────────────────────────────────────────────────────
//  perturb — produce a jittered copy of the event sequence
//
//  Every add-order's price is independently multiplied by a random factor
//  drawn from [1 - jitter, 1 + jitter].  Cancel events don't carry a
//  meaningful price so they are left unchanged; their theirId still refers
//  to the matching add event, so referential integrity is preserved.
//
//  Why independent per-order jitter rather than a single per-run offset?
//  A single global offset shifts all prices together, so the relative order
//  of bids and asks is unchanged and the same branches are taken/skipped.
//  Independent jitter scrambles the crossing relationship between individual
//  bids and asks, which is what actually drives branch outcomes inside
//  matchOrder() and the heap comparators.
//
//  Why not jitter quantities too?  Price determines whether a match occurs
//  at all (the most critical branch).  Quantity only affects how many loop
//  iterations matchOrder() runs once a match is confirmed.  Price jitter
//  alone is sufficient to prevent the predictor settling.
// ─────────────────────────────────────────────────────────────────────────────

std::vector<Event> perturb(const std::vector<Event>& events,
                            std::mt19937&              rng,
                            double                     jitter_pct)
{
    std::uniform_real_distribution<double> dist(1.0 - jitter_pct,
                                                1.0 + jitter_pct);
    std::vector<Event> out = events;   // copy — cancel events need no change
    for (auto& e : out) {
        if (e.type != 1) continue;     // only add-orders have meaningful prices
        double newPrice = std::round(static_cast<double>(e.price) * dist(rng));
        e.price = static_cast<Price>(std::max(1.0, newPrice)); // clamp > 0
    }
    return out;
}


// ─────────────────────────────────────────────────────────────────────────────
//  runOnce — process a (possibly jittered) event list; return elapsed ns
//
//  Called with stdout already suppressed by the caller.
// ─────────────────────────────────────────────────────────────────────────────

long long runOnce(const std::vector<Event>& events) {
    OrderBook<Quantity>         book;
    std::unordered_map<int, Id> idsMap;
    idsMap.reserve(events.size());

    auto start = std::chrono::high_resolution_clock::now();

    for (const auto& e : events) {
        if (e.type == 1) {
            Id id = book.addOrder(OrderType::goodTillCanceled,
                                  e.side, e.price, e.quantity);
            idsMap.emplace(e.theirId, id);
        } else {
            auto it = idsMap.find(e.theirId);
            if (it != idsMap.end())
                book.cancelOrder(it->second);
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
}


// ─────────────────────────────────────────────────────────────────────────────
//  Stats
// ─────────────────────────────────────────────────────────────────────────────

struct Stats {
    long long min_ns, max_ns;
    double    mean_ns, median_ns, stddev_ns, p95_ns, p99_ns;
    double    throughput_per_sec;
};

Stats computeStats(std::vector<long long> samples, size_t eventCount) {
    std::sort(samples.begin(), samples.end());
    const size_t n = samples.size();

    Stats s{};
    s.min_ns = samples.front();
    s.max_ns = samples.back();

    double sum = 0.0;
    for (long long t : samples) sum += static_cast<double>(t);
    s.mean_ns = sum / static_cast<double>(n);

    s.median_ns = (n % 2 == 0)
        ? (samples[n/2 - 1] + samples[n/2]) / 2.0
        : static_cast<double>(samples[n/2]);

    double var = 0.0;
    for (long long t : samples) {
        double d = static_cast<double>(t) - s.mean_ns;
        var += d * d;
    }
    s.stddev_ns = std::sqrt(var / static_cast<double>(n - 1));

    auto pctile = [&](double p) {
        size_t idx = static_cast<size_t>(std::ceil(p * static_cast<double>(n))) - 1;
        return static_cast<double>(samples[std::min(idx, n - 1)]);
    };
    s.p95_ns = pctile(0.95);
    s.p99_ns = pctile(0.99);

    s.throughput_per_sec = static_cast<double>(eventCount) / (s.mean_ns / 1e9);
    return s;
}


// ─────────────────────────────────────────────────────────────────────────────
//  ASCII histogram
// ─────────────────────────────────────────────────────────────────────────────

void printHistogram(const std::vector<long long>& sorted_ns, int buckets = 10) {
    if (sorted_ns.size() < 2 || sorted_ns.front() == sorted_ns.back()) return;

    double lo    = static_cast<double>(sorted_ns.front());
    double hi    = static_cast<double>(sorted_ns.back());
    double width = (hi - lo) / buckets;

    std::vector<int> counts(buckets, 0);
    for (long long t : sorted_ns) {
        int bin = static_cast<int>((static_cast<double>(t) - lo) / width);
        ++counts[std::min(bin, buckets - 1)];
    }

    int maxCount = *std::max_element(counts.begin(), counts.end());
    constexpr int BAR = 40;

    std::cout << "\n  Distribution of run times:\n\n";
    for (int i = 0; i < buckets; ++i) {
        double blo = (lo + i       * width) / 1e6;
        double bhi = (lo + (i + 1) * width) / 1e6;
        int    len = maxCount > 0 ? counts[i] * BAR / maxCount : 0;
        std::cout << "  "
                  << std::fixed << std::setprecision(2)
                  << std::setw(7) << blo << " – " << std::setw(7) << bhi << " ms  │"
                  << std::string(len, '>') << std::string(BAR - len, ' ')
                  << "│ " << counts[i] << "\n";
    }
}


// ─────────────────────────────────────────────────────────────────────────────
//  Results table
// ─────────────────────────────────────────────────────────────────────────────

void printResults(const Stats& s,
                  const std::vector<long long>& sorted_ns,
                  int warmup, int runs, size_t eventCount)
{
    auto fmtNs = [](double ns) -> std::string {
        std::ostringstream o;
        if (ns >= 1e6) o << std::fixed << std::setprecision(3) << (ns / 1e6) << " ms";
        else           o << std::fixed << std::setprecision(1) << (ns / 1e3) << " µs";
        return o.str();
    };
    auto row = [&](const std::string& label, double ns) {
        std::cout << "  " << std::left  << std::setw(20) << label
                  << std::right << std::setw(12) << fmtNs(ns) << "\n";
    };

    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════╗\n";
    std::cout << "║           OrderBook Benchmark Results              ║\n";
    std::cout << "╠════════════════════════════════════════════════════╣\n";
    std::cout << "  Events per run      : " << eventCount  << "\n";
    std::cout << "  Price jitter        : ±" << (JITTER_PCT * 100) << "%\n";
    std::cout << "  Warmup runs         : " << warmup      << "\n";
    std::cout << "  Benchmark runs      : " << runs        << "\n";
    std::cout << "╠════════════════════════════════════════════════════╣\n";
    row("Min",    static_cast<double>(s.min_ns));
    row("Max",    static_cast<double>(s.max_ns));
    row("Mean",   s.mean_ns);
    row("Median", s.median_ns);
    row("Std dev",s.stddev_ns);
    row("p95",    s.p95_ns);
    row("p99",    s.p99_ns);
    std::cout << "╠════════════════════════════════════════════════════╣\n";
    {
        double tp = s.throughput_per_sec;
        std::cout << "  " << std::left << std::setw(20) << "Throughput";
        if (tp >= 1e6)
            std::cout << std::right << std::setw(9) << std::fixed
                      << std::setprecision(2) << (tp / 1e6) << " M events/sec\n";
        else
            std::cout << std::right << std::setw(9) << std::fixed
                      << std::setprecision(2) << (tp / 1e3) << " K events/sec\n";
    }
    double cv = (s.stddev_ns / s.mean_ns) * 100.0;
    std::cout << "  " << std::left  << std::setw(20) << "CV (stddev/mean)"
              << std::right << std::setw(9) << std::fixed
              << std::setprecision(1) << cv << " %\n";
    std::cout << "╚════════════════════════════════════════════════════╝\n";

    printHistogram(sorted_ns);
}


// ─────────────────────────────────────────────────────────────────────────────
//  main
// ─────────────────────────────────────────────────────────────────────────────


// ─────────────────────────────────────────────────────────────────────────────
//  writeJson — emit raw timings + stats for the Python visualiser
//
//  Writing our own minimal JSON avoids a third-party library.
//  Python reads "raw_timings_ns" for per-run data and "stats" for aggregates.
// ─────────────────────────────────────────────────────────────────────────────

static const std::string JSON_OUT = "bench_results.json";

void writeJson(const std::string&            outfile,
               const Stats&                  s,
               const std::vector<long long>& timings,
               size_t                        eventCount)
{
    std::ofstream f(outfile);
    if (!f.is_open()) { std::cerr << "Warning: could not write " << outfile << "\n"; return; }
    f << std::fixed << std::setprecision(6);
    f << "{\n";
    f << "  \"config\": {\n";
    f << "    \"events_per_run\": "  << eventCount     << ",\n";
    f << "    \"jitter_pct\": "      << JITTER_PCT     << ",\n";
    f << "    \"warmup_runs\": "     << WARMUP_RUNS    << ",\n";
    f << "    \"benchmark_runs\": "  << BENCHMARK_RUNS << "\n";
    f << "  },\n";
    f << "  \"stats\": {\n";
    f << "    \"min_ms\": "               << (s.min_ns           / 1e6) << ",\n";
    f << "    \"max_ms\": "               << (s.max_ns           / 1e6) << ",\n";
    f << "    \"mean_ms\": "              << (s.mean_ns          / 1e6) << ",\n";
    f << "    \"median_ms\": "            << (s.median_ns        / 1e6) << ",\n";
    f << "    \"stddev_ms\": "            << (s.stddev_ns        / 1e6) << ",\n";
    f << "    \"p95_ms\": "               << (s.p95_ns           / 1e6) << ",\n";
    f << "    \"p99_ms\": "               << (s.p99_ns           / 1e6) << ",\n";
    f << "    \"throughput_per_sec\": "   << s.throughput_per_sec       << ",\n";
    f << "    \"cv_pct\": "               << ((s.stddev_ns / s.mean_ns) * 100.0) << "\n";
    f << "  },\n";
    f << "  \"raw_timings_ns\": [";
    for (size_t i = 0; i < timings.size(); ++i) {
        f << timings[i];
        if (i + 1 < timings.size()) f << ",";
    }
    f << "]\n}\n";
    std::cerr << "Results written to " << outfile << "\n";
}

int main(int argc, char* argv[]) {
    const std::string filename = (argc > 1) ? argv[1] : DATA_FILE;

    // ── 1. Load ───────────────────────────────────────────────────────────────
    std::cerr << "Loading " << filename << " ...\n";
    std::vector<Event> events;
    try {
        events = loadEvents(filename);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    std::cerr << "Loaded " << events.size() << " events.\n\n";

    // Seed from hardware entropy so runs differ across processes too
    std::mt19937 rng(std::random_device{}());

    std::vector<long long> timings;

    // ── 2. Warmup (jittered, discarded) ──────────────────────────────────────
    // Using jittered data for warmup primes caches and the branch predictor
    // with the *style* of the workload (lots of heap ops, hash lookups) without
    // teaching it the exact branch sequence that the benchmark runs will use.
    {
        SuppressStdout mute;
        std::cerr << "Warming up (" << WARMUP_RUNS << " runs, jittered) ...\n";
        for (int i = 0; i < WARMUP_RUNS; ++i) {
            auto jittered = perturb(events, rng, JITTER_PCT);
            runOnce(jittered);
        }
    }

    // ── 3. Benchmark (each run gets a fresh jitter) ───────────────────────────
    // perturb() is called outside the timed window so dataset-generation cost
    // is never counted.  SuppressStdout mutes the book's internal cout.
    {
        SuppressStdout mute;
        std::cerr << "Benchmarking (" << BENCHMARK_RUNS << " runs, jittered) ...\n";
        timings.reserve(BENCHMARK_RUNS);
        for (int i = 0; i < BENCHMARK_RUNS; ++i) {
            auto jittered = perturb(events, rng, JITTER_PCT); // ← not timed
            timings.push_back(runOnce(jittered));              // ← timed
            if ((i + 1) % 10 == 0)
                std::cerr << "  " << (i + 1) << " / "
                          << BENCHMARK_RUNS << " done\n";
        }
    }  // ← SuppressStdout destructs here; cout restored before printResults

    // ── 4. Report ─────────────────────────────────────────────────────────────
    std::vector<long long> sorted = timings;
    std::sort(sorted.begin(), sorted.end());
    Stats s = computeStats(timings, events.size());
    printResults(s, sorted, WARMUP_RUNS, BENCHMARK_RUNS, events.size());
    writeJson(JSON_OUT, s, timings, events.size());

    return 0;
}
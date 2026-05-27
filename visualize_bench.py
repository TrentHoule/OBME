#!/usr/bin/env python3
"""
visualize_bench.py
──────────────────
Compiles bench_orderbook.cpp, runs it, reads the JSON it produces, and
draws four diagnostic plots:

  1. Histogram + KDE   — full distribution with statistical markers
  2. Run-order scatter — each timed run in sequence; reveals trends
  3. CDF               — read off any percentile visually
  4. Stats table       — key numbers in one place

Usage:
  python visualize_bench.py                    # compile + run + plot
  python visualize_bench.py --skip-compile     # already compiled
  python visualize_bench.py --skip-run         # reuse existing bench_results.json
  python visualize_bench.py --out fig.png      # save instead of showing interactively
  python visualize_bench.py --help
"""

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

import numpy as np
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
import matplotlib.ticker as mticker
from matplotlib.lines import Line2D
from matplotlib.patches import FancyBboxPatch


# ─────────────────────────────────────────────────────────────────────────────
#  Configuration — mirror what bench_orderbook.cpp uses
# ─────────────────────────────────────────────────────────────────────────────

BENCH_SRC    = "benchmark.cpp"
BENCH_BIN    = "./bench_orderbook"
RESULTS_JSON = "bench_results.json"

# Compilation command
COMPILE_CMD = [
    "g++", "-std=c++20", "-O2",
    "-o", "bench_orderbook",
    BENCH_SRC,
]

# Visual palette
C = {
    "bars":      "#4C9ED9",   # histogram bars
    "kde":       "#1565C0",   # KDE line
    "iqr":       "#BBDEFB",   # IQR shading on histogram
    "mean":      "#E53935",   # mean line
    "median":    "#43A047",   # median line
    "p95":       "#8E24AA",   # p95 marker
    "p99":       "#F57C00",   # p99 marker
    "scatter":   "#4C9ED9",   # run-order dots
    "trend":     "#E53935",   # rolling mean on run-order plot
    "cdf_line":  "#1565C0",   # CDF curve
    "grid":      "#EEEEEE",
    "panel_bg":  "#F5F5F5",
    "text":      "#212121",
}


# ─────────────────────────────────────────────────────────────────────────────
#  Step 1 — compile
# ─────────────────────────────────────────────────────────────────────────────

def compile_benchmark():
    print(f"Compiling {BENCH_SRC} ...")
    result = subprocess.run(COMPILE_CMD, capture_output=True, text=True)
    if result.returncode != 0:
        print("── Compilation failed ──────────────────────────")
        print(result.stderr)
        sys.exit(1)
    print("Compiled successfully.\n")


# ─────────────────────────────────────────────────────────────────────────────
#  Step 2 — run
# ─────────────────────────────────────────────────────────────────────────────

def run_benchmark():
    print(f"Running {BENCH_BIN} ...")
    # stderr stays connected so progress messages appear in the terminal.
    # stdout (the pretty-print table) is also left connected.
    result = subprocess.run([BENCH_BIN])
    if result.returncode != 0:
        print("Benchmark exited with a non-zero status.")
        sys.exit(1)
    print()


# ─────────────────────────────────────────────────────────────────────────────
#  Step 3 — load results
# ─────────────────────────────────────────────────────────────────────────────

def load_results(path: str) -> dict:
    if not Path(path).exists():
        print(f"Error: {path} not found.  Run the benchmark first.")
        sys.exit(1)
    with open(path) as f:
        return json.load(f)


# ─────────────────────────────────────────────────────────────────────────────
#  Shared axis styling helper
# ─────────────────────────────────────────────────────────────────────────────

def _style(ax, title: str, xlabel: str, ylabel: str):
    ax.set_facecolor(C["panel_bg"])
    ax.set_title(title, fontsize=11, fontweight="bold",
                 color=C["text"], pad=8)
    ax.set_xlabel(xlabel, fontsize=9, color=C["text"])
    ax.set_ylabel(ylabel, fontsize=9, color=C["text"])
    ax.tick_params(colors=C["text"], labelsize=8)
    ax.grid(color=C["grid"], linewidth=0.7, zorder=0)
    for spine in ax.spines.values():
        spine.set_edgecolor("#CCCCCC")


# ─────────────────────────────────────────────────────────────────────────────
#  Plot 1 — Histogram + KDE
#
#  Shows the full shape of the timing distribution.  Vertical lines mark the
#  key statistics so you can see at a glance whether the mean is being pulled
#  by outliers (mean >> median) and how fat the right tail is (p99 vs p95).
#
#  The IQR shading (p25–p75) shows where the middle 50% of runs fall.
# ─────────────────────────────────────────────────────────────────────────────

def plot_histogram(ax, raw_ms: np.ndarray, stats: dict):
    n_bins = max(15, int(np.sqrt(len(raw_ms)) * 2))
    counts, edges, patches = ax.hist(
        raw_ms, bins=n_bins,
        color=C["bars"], alpha=0.75, edgecolor="white", linewidth=0.5,
        zorder=2, label="Run times"
    )

    # KDE overlay — use numpy to compute a smooth density curve
    # Gaussian KDE: convolve a fine grid of x-values with a kernel of
    # bandwidth chosen by Scott's rule (h = n^(-1/5) * std).
    bandwidth = 1.06 * raw_ms.std() * len(raw_ms) ** (-0.2)
    x_grid    = np.linspace(raw_ms.min() - bandwidth, raw_ms.max() + bandwidth, 400)
    kde_vals  = np.zeros_like(x_grid)
    for xi in raw_ms:
        kde_vals += np.exp(-0.5 * ((x_grid - xi) / bandwidth) ** 2)
    kde_vals /= (len(raw_ms) * bandwidth * np.sqrt(2 * np.pi))
    # Scale KDE to match histogram y-axis (density → count)
    bin_width = edges[1] - edges[0]
    kde_vals  *= len(raw_ms) * bin_width
    ax.plot(x_grid, kde_vals, color=C["kde"], linewidth=2, zorder=3, label="KDE")

    # IQR shading
    p25, p75 = np.percentile(raw_ms, [25, 75])
    ax.axvspan(p25, p75, color=C["iqr"], alpha=0.4, zorder=1, label="IQR (p25–p75)")

    # Statistical marker lines
    markers = [
        ("mean",   stats["mean_ms"],   C["mean"],   "--", ""),
        ("median", stats["median_ms"], C["median"], "-",  ""),
        ("p95",    stats["p95_ms"],    C["p95"],    ":",  "p95"),
        ("p99",    stats["p99_ms"],    C["p99"],    ":",  "p99"),
    ]
    y_top = counts.max()
    i = 0.05
    for key, val, colour, ls, label in markers:
        ax.axvline(val, color=colour, linestyle=ls, linewidth=1.8, zorder=4)
        ax.text(val, y_top * (0.97 - i), f" {label}\n {val:.3f}ms",
                color=colour, fontsize=7.5, va="top", fontweight="bold")
        i += 0.05

    _style(ax,
           title="Distribution of Run Times",
           xlabel="Time (ms)",
           ylabel="Count")

    legend_handles = [
        Line2D([0], [0], color=C["kde"],    linewidth=2,              label="KDE"),
        Line2D([0], [0], color=C["iqr"],    linewidth=8, alpha=0.4,   label="IQR (p25–p75)"),
        Line2D([0], [0], color=C["mean"],   linewidth=1.8, ls="--",   label="Mean"),
        Line2D([0], [0], color=C["median"], linewidth=1.8,            label="Median"),
        Line2D([0], [0], color=C["p95"],    linewidth=1.8, ls=":",    label="p95"),
        Line2D([0], [0], color=C["p99"],    linewidth=1.8, ls=":",    label="p99"),
    ]
    ax.legend(handles=legend_handles, fontsize=8,
              loc="upper right", framealpha=0.85)


# ─────────────────────────────────────────────────────────────────────────────
#  Plot 2 — Run-order time series
#
#  X-axis is run index (1 … N), Y-axis is that run's time.  A rolling mean
#  (window = 10% of runs) is overlaid.
#
#  What to look for:
#   • Downward trend early on → warmup wasn't quite enough; consider raising
#     WARMUP_RUNS.
#   • Sudden spikes later → OS scheduling preemption or thermal throttling.
#   • Slow upward drift → thermal throttling on a sustained workload.
#   • Flat with noise → clean, trustworthy data.
# ─────────────────────────────────────────────────────────────────────────────

def plot_run_order(ax, raw_ms: np.ndarray, stats: dict):
    x = np.arange(1, len(raw_ms) + 1)

    ax.scatter(x, raw_ms, color=C["scatter"], s=18, alpha=0.6, zorder=3)
    ax.plot(x, raw_ms, color=C["scatter"], linewidth=0.5, alpha=0.3, zorder=2)

    # Rolling mean
    window = max(3, len(raw_ms) // 10)
    kernel = np.ones(window) / window
    rolling = np.convolve(raw_ms, kernel, mode="valid")
    x_roll  = np.arange(window, len(raw_ms) + 1)
    ax.plot(x_roll, rolling, color=C["trend"], linewidth=2,
            zorder=4, label=f"Rolling mean (w={window})")

    ax.axhline(stats["mean_ms"], color=C["mean"], linestyle="--",
               linewidth=1.5, zorder=4, label=f'Mean = {stats["mean_ms"]:.3f} ms')

    _style(ax,
           title="Run Times in Order",
           xlabel="Run #",
           ylabel="Time (ms)")
    ax.legend(fontsize=8, framealpha=0.85)
    ax.yaxis.set_major_formatter(mticker.FormatStrFormatter("%.2f"))


# ─────────────────────────────────────────────────────────────────────────────
#  Plot 3 — CDF (Cumulative Distribution Function)
#
#  The CDF answers "what fraction of runs finished in ≤ X ms?" — the inverse
#  of the question the histogram answers.  It's easier to read off exact
#  percentile values from a CDF than from a histogram.
#
#  Crosshairs mark p50, p95, and p99.
# ─────────────────────────────────────────────────────────────────────────────

def plot_cdf(ax, raw_ms: np.ndarray, stats: dict):
    sorted_ms = np.sort(raw_ms)
    y = np.arange(1, len(sorted_ms) + 1) / len(sorted_ms)

    ax.plot(sorted_ms, y, color=C["cdf_line"], linewidth=2, zorder=3)
    ax.fill_between(sorted_ms, y, alpha=0.12, color=C["cdf_line"], zorder=2)

    crosshairs = [
        (stats["median_ms"], 0.50, C["median"], "p50"),
        (stats["p95_ms"],    0.95, C["p95"],    "p95"),
        (stats["p99_ms"],    0.99, C["p99"],    "p99"),
    ]
    for val, prob, colour, label in crosshairs:
        ax.axhline(prob, xmax=(val - sorted_ms.min()) /
                             (sorted_ms.max() - sorted_ms.min()),
                   color=colour, linestyle=":", linewidth=1.2, zorder=4)
        ax.axvline(val, ymax=prob, color=colour,
                   linestyle=":", linewidth=1.2, zorder=4)
        ax.plot(val, prob, "o", color=colour, markersize=6, zorder=5)
        ax.text(val, prob + 0.025, f" {label}\n {val:.3f}ms",
                color=colour, fontsize=7.5, fontweight="bold", va="bottom")

    ax.set_ylim(0, 1.08)
    ax.yaxis.set_major_formatter(mticker.PercentFormatter(xmax=1))
    ax.xaxis.set_major_formatter(mticker.FormatStrFormatter("%.2f"))
    _style(ax, title="CDF", xlabel="Time (ms)", ylabel="Cumulative %")


# ─────────────────────────────────────────────────────────────────────────────
#  Plot 4 — Stats table (rendered as a matplotlib table, no axes)
# ─────────────────────────────────────────────────────────────────────────────

def plot_stats_table(ax, stats: dict, cfg: dict):
    ax.axis("off")

    tp = stats["throughput_per_sec"]
    if tp >= 1e6:
        tp_str = f"{tp/1e6:.2f} M events/sec"
    else:
        tp_str = f"{tp/1e3:.2f} K events/sec"

    rows = [
        ("Events per run",  f"{cfg['events_per_run']:,}"),
        ("Price jitter",    f"±{cfg['jitter_pct']*100:.0f}%"),
        ("Warmup runs",     str(cfg["warmup_runs"])),
        ("Benchmark runs",  str(cfg["benchmark_runs"])),
        ("", ""),  # spacer
        ("Min",             f"{stats['min_ms']:.3f} ms"),
        ("Max",             f"{stats['max_ms']:.3f} ms"),
        ("Mean",            f"{stats['mean_ms']:.3f} ms"),
        ("Median",          f"{stats['median_ms']:.3f} ms"),
        ("Std dev",         f"{stats['stddev_ms']:.3f} ms"),
        ("p95",             f"{stats['p95_ms']:.3f} ms"),
        ("p99",             f"{stats['p99_ms']:.3f} ms"),
        ("", ""),  # spacer
        ("Throughput",      tp_str),
        ("CV (σ/μ)",        f"{stats['cv_pct']:.1f}%"),
    ]

    # Colour-code CV: green=good, amber=noisy, red=very noisy
    cv = stats["cv_pct"]
    cv_colour = "#43A047" if cv < 5 else ("#FB8C00" if cv < 15 else "#E53935")

    cell_text  = [[r[0], r[1]] for r in rows]
    cell_cols  = [["#ECEFF1", "#FAFAFA"]] * len(rows)
    # Highlight CV row
    cv_idx = next(i for i, r in enumerate(rows) if r[0] == "CV (σ/μ)")
    cell_cols[cv_idx] = ["#ECEFF1", cv_colour + "33"]  # 33 = ~20% opacity

    tbl = ax.table(
        cellText=cell_text,
        colLabels=["Metric", "Value"],
        cellColours=cell_cols,
        loc="center",
        cellLoc="left",
    )
    tbl.auto_set_font_size(False)
    tbl.set_fontsize(9)
    tbl.scale(1, 1.35)

    # Style the header row
    for col in range(2):
        cell = tbl[0, col]
        cell.set_facecolor("#1565C0")
        cell.set_text_props(color="white", fontweight="bold")

    ax.set_title("Summary", fontsize=11, fontweight="bold",
                 color=C["text"], pad=8)


# ─────────────────────────────────────────────────────────────────────────────
#  Main plotting function
# ─────────────────────────────────────────────────────────────────────────────

def plot_results(data: dict, output_path: str | None):
    cfg   = data["config"]
    stats = data["stats"]
    raw   = np.array(data["raw_timings_ns"]) / 1e6   # ns → ms

    fig = plt.figure(figsize=(15, 10), facecolor="white")
    fig.suptitle(
        f"OrderBook Benchmark  ·  "
        f"{cfg['benchmark_runs']} runs  ·  "
        f"{cfg['events_per_run']:,} events/run  ·  "
        f"±{cfg['jitter_pct']*100:.0f}% price jitter",
        fontsize=13, fontweight="bold", color=C["text"], y=0.99,
    )

    # Layout: top row = histogram + stats table (3:1 width)
    #         bottom row = run-order + CDF (1:1 width)
    gs = gridspec.GridSpec(
        2, 2,
        figure=fig,
        width_ratios=[3, 1],
        height_ratios=[1, 1],
        hspace=0.38,
        wspace=0.28,
        left=0.07, right=0.97,
        top=0.94, bottom=0.07,
    )

    ax_hist  = fig.add_subplot(gs[0, 0])
    ax_table = fig.add_subplot(gs[0, 1])
    ax_run   = fig.add_subplot(gs[1, 0])
    ax_cdf   = fig.add_subplot(gs[1, 1])

    plot_histogram(ax_hist,  raw, stats)
    plot_stats_table(ax_table, stats, cfg)
    plot_run_order(ax_run,   raw, stats)
    plot_cdf(ax_cdf,         raw, stats)

    if output_path:
        fig.savefig(output_path, dpi=150, bbox_inches="tight")
        print(f"Figure saved to {output_path}")
    else:
        plt.show()


# ─────────────────────────────────────────────────────────────────────────────
#  Entry point
# ─────────────────────────────────────────────────────────────────────────────

def parse_args():
    p = argparse.ArgumentParser(
        description="Compile, run, and visualise the OrderBook benchmark."
    )
    p.add_argument("--skip-compile", action="store_true",
                   help="Skip compilation (use existing binary).")
    p.add_argument("--skip-run",     action="store_true",
                   help="Skip running the benchmark (reuse bench_results.json).")
    p.add_argument("--out", metavar="FILE",
                   help="Save figure to FILE instead of showing interactively.")
    return p.parse_args()


def main():
    args = parse_args()

    if not args.skip_compile:
        compile_benchmark()

    if not args.skip_run:
        run_benchmark()

    print(f"Loading {RESULTS_JSON} ...")
    data = load_results(RESULTS_JSON)
    print(f"Loaded {len(data['raw_timings_ns'])} run timings.\n")

    plot_results(data, args.out)


if __name__ == "__main__":
    main()
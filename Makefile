CXX			= clang++
CXXFLAGS	= -std=c++20 -Wall -Wextra -Wpedantic

all: runSampleData testOrderBook benchmark

# main: main.cpp orderBook.h types.h order.h trade.h
# 	$(CXX) $(CXXFLAGS) main.cpp -o main

runSampleData: runSampleData.cpp orderBook.h types.h order.h trade.h
	$(CXX) $(CXXFLAGS) runSampleData.cpp -o runSampleData

testOrderBook: testOrderBook.cpp orderBook.h types.h order.h trade.h
	$(CXX) $(CXXFLAGS) testOrderBook.cpp -o testOrderBook

benchmark: benchmark.cpp orderBook.h types.h order.h trade.h
	$(CXX) $(CXXFLAGS) -O2 benchmark.cpp -o benchmark

.PHONY: clean all

clean:
	-rm -f runSampleData testOrderBook benchmark
CXX			= clang++
CXXFLAGS	= -std=c++20 -Wall -Wextra -Wpedantic

main: main.cpp orderBook.h types.h order.h trade.h
	$(CXX) $(CXXFLAGS) main.cpp -o main

runSampleData: runSampleData.cpp orderBook.h types.h order.h trade.h
	$(CXX) $(CXXFLAGS) runSampleData.cpp -o runSampleData

testOrderBook: testOrderBook.cpp orderBook.h types.h order.h trade.h
	$(CXX) $(CXXFLAGS) testOrderBook.cpp -o testOrderBook

.PHONY: clean

clean:
	-rm -f main runSampleData testOrderBook
CXX			= clang++
CXXFLAGS	= -std=c++20 -Wall -Wextra -Wpedantic

main: main.cpp orderBook.h types.h order.h
	$(CXX) $(CXXFLAGS) main.cpp -o main

.PHONY: clean

clean:
	-rm -f main
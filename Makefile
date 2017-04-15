CXX = g++
CXXFLAGS = -std=c++14 -O2 -Wall -s
LDFLAGS = -lwiringPi -lpthread
SOURCES = garaged.cpp events.cpp main.cpp
HEADERS = garaged.h events.h

all: garaged

garaged: $(SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $@ $(LDFLAGS)

.PHONY: all
# tapemgr - create, scan, and extract AWS tape images
#
#   make          build ./tapemgr
#   make test     build and run the test suite (needs bash and python3)
#   make clean

CXX      ?= c++
CXXFLAGS ?= -O2 -Wall
CPPFLAGS += -std=c++17 -Ithird_party

SRCS = src/tapemgr.cpp \
       src/ebcdic_converter.cpp \
       src/cp037_tables.cpp \
       src/cp273_tables.cpp \
       src/cp277_tables.cpp \
       src/cp285_tables.cpp
OBJS = $(SRCS:.cpp=.o)

all: tapemgr

tapemgr: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

src/%.o: src/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

src/tapemgr.o: src/ebcdic_converter.h third_party/nlohmann/json.hpp
src/ebcdic_converter.o: src/ebcdic_converter.h src/cp037_tables.h src/cp273_tables.h src/cp277_tables.h src/cp285_tables.h
src/cp037_tables.o: src/cp037_tables.h
src/cp273_tables.o: src/cp273_tables.h
src/cp277_tables.o: src/cp277_tables.h
src/cp285_tables.o: src/cp285_tables.h

test: tapemgr
	./run_tests.sh

clean:
	rm -f tapemgr $(OBJS)
	rm -rf tests/output tests/data tests/config

.PHONY: all test clean

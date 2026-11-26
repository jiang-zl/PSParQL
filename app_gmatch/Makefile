CCOMPILE=mpic++
CPPFLAGS= -I$(HADOOP_HOME)/include -I$(JAVA_HOME)/include -I$(JAVA_HOME)/include/linux -I ../system -Wno-deprecated -O2
LIB = -L$(HADOOP_HOME)/lib/native
LDFLAGS = -lhdfs

all: run

run: run.cpp Makefile $(wildcard *.h ../system/*.h ../system/subgraph/*.h ../system/util/*.h)
	$(CCOMPILE) -std=c++11 -pthread run.cpp $(CPPFLAGS) $(CXXFLAGS) $(LIB) $(LDFLAGS)  -o run
	
clean:
	-rm run

CC:=g++
CXXFLAGS:= -std=c++11 -g
test_connect: test_connect.cpp

clean:
	-rm *.o
	-rm test_connect

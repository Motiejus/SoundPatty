# vim: set noet:
DEPS=`pkg-config --libs jack` -llog4cxx -std=gnu++0x -g

all: main controller

main: main.o soundpatty.o input.o
	g++ ${DEPS} main.o soundpatty.o input.o -o main
main.o: main.cpp main.h input.h soundpatty.h
	g++ ${DEPS} -c main.cpp
soundpatty.o: main.h soundpatty.cpp soundpatty.h input.h
	g++ ${DEPS} -c soundpatty.cpp
input.o: main.h input.cpp input.h soundpatty.h
	g++ ${DEPS} -c input.cpp

clean:
	rm -f main soundpatty.o input.o main.o controller controller.o

controller: controller.o soundpatty.o input.o
	g++ ${DEPS} controller.o soundpatty.o input.o -o controller
controller.o: controller.cpp controller.h
	g++ ${DEPS} -c controller.cpp

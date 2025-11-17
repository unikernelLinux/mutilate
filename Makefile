O          := 0
CFLAGS     += -g -O${O} -std=gnu99 -MD -MP -Wall
CXXFLAGS   += -g -O${O} -MD -MP -Wall
LDFLAGS    += -lpthread

TARGETS := monloop.o 

all: ${TARGETS}

monloop.o: monloop.c 
	${CC} ${CFLAGS} -c -o $@ $^ 

clean:
	-rm -rf $(wildcard *.o *.d ${TARGETS})

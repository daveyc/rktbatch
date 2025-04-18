CPP=ibm-clang++
CFLAGS=-O -qasm -m32 -mzos-asmlib="//'SYS1.MACLIB'" -D_EXT
LOADLIB="//'${USER}.LOAD(RKTBATCH)'"

DEPS = include/pipe.h include/file.h include/errors.h

all: rktbatch

%.o: %.cpp
		$(CPP) -c -o $@ $< $(CFLAGS)

rktbatch: main.o
		$(CPP) -o rktbatch main.o

clean:
	rm *.o rktbatch
	
install: rktbatch
	cp rktbatch ${LOADLIB}

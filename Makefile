CPP=ibm-clang++ -m32
CFLAGS=-O -mzos-no-asm-implicit-clobber-reg -mzos-asmlib="//'SYS1.MACLIB'" -D_EXT -D_XOPEN_SOURCE_EXTENDED  -D_ALL_SOURCE -D_OPEN_MSGQ_EXT
LOADLIB="//'${USER}.LOAD(RKTBATCH)'"

DEPS = include/pipe.h include/file.h include/errors.h

all: rktbatch

%.o: %.cpp
		$(CPP) -c -o $@ $< $(CFLAGS)

rktbatch: main.o
		$(CPP) -o rktbatch main.o

clean:
	rm -f *.o rktbatch
	
install: rktbatch
	cp rktbatch ${LOADLIB}

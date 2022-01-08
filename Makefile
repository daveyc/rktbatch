CPP=xlC
CFLAGS=-O -qlang=newexcp -qasm -qasmlib="//'SYS1.MACLIB'" -qlang=extended0x -D_ALL_SOURCE -D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENDED -D__IBMCPP_TR1__ -D_UNIX03_THREADS -qlang=libext  -D_OPEN_SYS_SOCK_EXT2 -D__OPEN_SYS_SOCK_EXT -D_OPEN_SYS_SOCK_IPV6
LOADLIB="//'${USER}.LOAD(RKTBATCH)'"

DEPS = pipe.h file.h errors.h

all: rktbatch

%.o: %.cpp
		$(CPP) -c -o $@ $< $(CFLAGS)

rktbatch: main.o
		$(CPP) -o rktbatch main.o

clean:
	rm *.o rktbatch
	
install: rktbatch
	cp rktbatch ${LOADLIB}

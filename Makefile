CPP=ibm-clang++ -m32
CFLAGS=--std=c++17 -MMD -O -I./include -I./argparse/include  -I./spdlog/include -mzos-float-kind=ieee -Wno-constant-conversion -mzos-no-asm-implicit-clobber-reg -mzos-asmlib="//'SYS1.MACLIB'" -D_EXT -D_XOPEN_SOURCE_EXTENDED  -D_ALL_SOURCE -D_OPEN_MSGQ_EXT -DSPDLOG_NO_TLS
LOADLIB="//'${USER}.LOAD(RKTBATCH)'"

OBJS := main.o
DEPS := $(patsubst %.o,%.d,$(OBJS))

all: rktbatch

%.o: %.cpp
		$(CPP) -c -o $@ $< $(CFLAGS)

# Include the generated dependency files
# The '-' makes 'make' ignore this line if the .d files don't exist yet
-include $(DEPS)

rktbatch: $(OBJS)
		$(CPP) -o rktbatch main.o

clean:
	rm -f *.o rktbatch
	
install: rktbatch
	cp rktbatch ${LOADLIB}

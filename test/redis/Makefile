SOURCES = $(wildcard *.c)
EXES = $(patsubst %.c, %, $(SOURCES))

all : $(EXES)

test : all
	./redis_all.sh

% : %.c marshal.h
	gcc $< -o $@ -std=c99

clean:
	rm -fv $(EXES)

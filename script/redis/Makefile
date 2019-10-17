SOURCES = $(wildcard *.c)
EXES = $(patsubst %.c, %, $(SOURCES))

ALL: $(EXES)

% : %.c marshal.h
	gcc $< -o $@ -std=c99

clean:
	rm -fv $(EXES)

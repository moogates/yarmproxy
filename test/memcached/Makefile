SOURCES = $(wildcard *.c)
EXES = $(patsubst %.c, %, $(SOURCES))

all : $(EXES)

% : %.c
	gcc $< -o $@ -std=c99

clean:
	rm -fv $(EXES)

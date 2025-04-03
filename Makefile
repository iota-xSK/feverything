CC = gcc
CFLAGS = -I. -IzForth/src/zforth
LIBS = -lraylib -lm -lreadline
OBJ_FILES = feverything.o zforth.o

all: $(OBJ_FILES)
	$(CC) $(LIBS) $(OBJ_FILES) -o feverything

feverything.o: main.c
	$(CC) $(CFLAGS) -c main.c -o feverything.o -DRAYGUI_IMPLEMENTATION

zforth.o: zForth/src/zforth/zforth.c
	$(CC) $(CFLAGS) -c zForth/src/zforth/zforth.c -o zforth.o

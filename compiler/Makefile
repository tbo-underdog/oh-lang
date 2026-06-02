CC = cc
CFLAGS = -O2 -Wall -Wextra -std=c11
SRC = src/main.c src/lexer.c src/parser.c src/typechecker.c src/codegen.c
HDR = src/lexer.h src/parser.h src/typechecker.h src/codegen.h src/common.h

.PHONY: all clean

all: overhaul

overhaul: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o overhaul $(SRC)

clean:
	rm -f overhaul *.c.bak

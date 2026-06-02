CC     = cc
CFLAGS = -O2 -Wall -Wextra -std=c11

# Reuse the compiler's lexer and parser directly (no typechecker or codegen needed)
COMPILER_SRC = ../compiler/src/lexer.c ../compiler/src/parser.c
TRANSLATOR_SRC = src/main.c

SRC = $(TRANSLATOR_SRC) $(COMPILER_SRC)

HDR = ../compiler/src/common.h \
      ../compiler/src/lexer.h  \
      ../compiler/src/parser.h

.PHONY: all clean

all: ohtranslate

ohtranslate: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -I../compiler/src -o ohtranslate $(SRC)

TESTS = ../tests/01_add.oh \
        ../tests/02_abs.oh \
        ../tests/03_sum.oh \
        ../tests/04_fibonacci.oh \
        ../tests/05_max_array.oh

run-tests: ohtranslate
	@for f in $(TESTS); do \
	    echo "=== $$f ==="; \
	    ./ohtranslate "$$f"; \
	    echo; \
	done

clean:
	rm -f ohtranslate

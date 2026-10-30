CC      ?= cc
CFLAGS  ?= -std=gnu11 -O2 -g -Wall -Wextra -Werror -Isrc
BIN     := build/ferret
SRCS    := src/main.c src/html.c src/css.c src/render.c src/fetch.c

all: $(BIN)

$(BIN): $(SRCS) src/html.h src/css.h src/render.h src/fetch.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRCS) -o $@

check: $(BIN)
	./tests/run-tests.sh

clean:
	rm -rf build

.PHONY: all check clean

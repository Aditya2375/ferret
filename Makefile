CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu11 -Wall -Wextra -Werror -Isrc
BIN     := build/ferret
SRCS    := src/main.c src/html.c src/css.c src/render.c src/fetch.c

# TLS (https://) via OpenSSL. Auto-detected: pass TLS=0 for a
# dependency-free build, or TLS_INC/TLS_LIB for a custom OpenSSL prefix.
TLS     ?= $(shell test -f $(or $(TLS_INC),/usr/include)/openssl/ssl.h && echo 1 || echo 0)
ifeq ($(TLS),1)
CFLAGS  += -DUSE_TLS $(if $(TLS_INC),-I$(TLS_INC),)
LDLIBS  += $(if $(TLS_LIB),-L$(TLS_LIB),) -lssl -lcrypto
endif

all: $(BIN)

$(BIN): $(SRCS) src/html.h src/css.h src/render.h src/fetch.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRCS) -o $@ $(LDLIBS)
	@echo "built $(BIN) (TLS=$(TLS))"

check: $(BIN)
	./tests/run-tests.sh

clean:
	rm -rf build

.PHONY: all check clean

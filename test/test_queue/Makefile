# pgagroal oversubscription test - build
#
#   make        - optimized build
#   make asan   - AddressSanitizer + UBSan build (recommended before CI runs)
#   make clean

CC      ?= gcc
PG_INC  ?= $(shell pg_config --includedir 2>/dev/null || echo /usr/include/postgresql)
PG_LIB  ?= $(shell pg_config --libdir 2>/dev/null || echo /usr/lib)
CFLAGS  ?= -O2 -g -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L
LDLIBS   = -lpq -lpthread -lm

all: oversubscription_test

oversubscription_test: oversubscription_test.c
	$(CC) $(CFLAGS) -I$(PG_INC) -L$(PG_LIB) -o $@ $< $(LDLIBS)

asan: oversubscription_test.c
	$(CC) -O1 -g -std=c11 -D_POSIX_C_SOURCE=200809L -fsanitize=address,undefined \
	      -I$(PG_INC) -L$(PG_LIB) -o oversubscription_test_asan $< $(LDLIBS)

clean:
	rm -f oversubscription_test oversubscription_test_asan

.PHONY: all asan clean

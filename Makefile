CHECK := $(shell which clang)

ifeq ($(CHECK),)
$(warning no clang found, consider apt-get install clang, using gcc now)
CC = gcc
else
$(info using clang over gcc)
CC = clang
endif

CFLAGS=-g -Wall -Wextra -pedantic -I./include
LDFLAGS=-g -L./build/src
LDLIBS=
RM=rm
BUILD_DIR=./build

.PHONY: test
test:
	$(MAKE) -C $@
	$(BUILD_DIR)/test/test_gc

coverage: test
	$(MAKE) -C test coverage

.PHONY: clean
clean:
	$(MAKE) -C test clean

distclean: clean
	$(MAKE) -C test distclean


CC=clang
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

coverage-html: coverage
	$(MAKE) -C test coverage-html

.PHONY: clean
clean:
	$(MAKE) -C test clean

distclean: clean
	$(MAKE) -C test distclean


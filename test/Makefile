CC=clang
CFLAGS=-g -Wall -Wextra -pedantic -I../include -fprofile-arcs -ftest-coverage
LDFLAGS=-g -L../build/src --coverage
LDLIBS=-ledit
RM=rm
BUILD_DIR=../build/test

.PHONY: all
all: $(BUILD_DIR)/test_gc

$(BUILD_DIR)/%.o: %.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -c $< -o $@

SRCS=test_gc.c ../src/log.c
OBJS=$(SRCS:%.c=$(BUILD_DIR)/%.o)
DEPS=$(OBJS:%.o=%.d)

$(BUILD_DIR)/test_gc: $(OBJS)
	mkdir -p $(@D)
	$(CC) $(LDFLAGS) $(LDLIBS) $^ -o $@

coverage: $(BUILD_DIR)/test_gc
	lcov -b . -d ../build/test/ -c -o ../build/test/coverage-all.info
	lcov -b . -r ../build/test/coverage-all.info "*test*" -o ../build/test/coverage.info
	mkdir -p ../build/test/coverage
	genhtml -o ../build/test/coverage ../build/test/coverage.info
	open ../build/test/coverage/index.html


.PHONY: clean
clean:
	$(RM) -f $(OBJS) $(DEPS)

distclean: clean
	$(RM) -f $(BUILD_DIR)/test/test_gc
	$(RM) -f $(BUILD_DIR)/test/*gcda
	$(RM) -f $(BUILD_DIR)/test/*gcno


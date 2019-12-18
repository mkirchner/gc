# gc
Zero-dependency garbage collection for C.

## Table of contents

...

## Introduction

`gc` is an implementation of a conservative, thread-local, mark-and-sweep
garbage collector. The implementation provides a fully functional replacement
for the standard POSIX `malloc()`, `calloc()`, `realloc()`, and `free()` calls.

The focus of `gc` is to provide a conceptually clean implementation of
a mark-and-sweep GC, without delving into the depths of architecture-specific
optimization (see e.g. the [Boehm GC][boehm] for such an undertaking). It
should be particularly suitable for learning purposes and is open for all kinds
of optimization (PRs welcome!).

The original motivation for `gc` is my desire to write [my own LISP][stutter]
in C, entirely from scratch - and that required garbage collection.


### Acknowledgements

This work would not have been possible without the ability to read the work of
others, most notably the [Boehm GC][boehm], orangeduck's [tgc][tgc] (which also
follows the ideals of being tiny and simple), and [The Garbage Collection
Handbook][garbage_collection_handbook].


## Documentation

* Read the [quickstart](#quickstart) below to see how to get started quickly
* The [concepts](#concepts) chapter describes the basic concepts and design
  decisions that went into the implementation of `gc`.
* Interleaved with the concepts, there are implementation sections that detail
  the implementation of the core components, see [hash map implementation](),
  [dumping regsiters on the stack](), [finding roots](), and [depth-first,
  recursive marking]().


## Quickstart

### Download and test

    $ git clone git@github.com:mkirchner/gc.git
    $ cd gc
    $ make test

### Basic usage

```c
...
#include "gc.h"
...


void some_fun() {
    ...
    int* my_array = gc_calloc(gc, 1024, sizeof(int));
    for (size_t i; i<1024; ++i) {
        my_array[i] = 42;
    }
    ...
    // no free/delete!
}

int main(int argc, char* argv[]) {
    gc = gc_start(gc, &argc);
    ...
    some_fun();
    ...
    gc_stop(gc);
    return 0;
}
```

## API

## Basic Concepts


* Scope is to *manage* memory allocation and deallocation, not to replace the
  allocation implementation, i.e. no re-implementation of malloc & friends
* This is accomplished by keeping track of memory allocation and periodically triggering
  deallocation for memory that is still allocated but unused.


## Hashmap implementation and private API

* separate chaining
* auto-resize

### Unused memory: the reachability definition

### Mark-and-sweep: basic algo

### Hash map implementation

### Finding roots

### Dumping registers on the stack

### Depth-first recursive marking


[boehm]: https://www.hboehm.info/gc/ 
[stutter]: https://github.com/mkirchner/stutter
[tgc]: https://github.com/orangeduck/tgc
[garbage_collection_handbook]: http://gchandbook.org

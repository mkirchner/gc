# gc: zero-dependency garbage collection for C

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


## Table of contents

* [Documentation Overview](#documentation-overview)
* [Quickstart](#quickstart)
  * [Download and test](#download-and-test)
  * [Basic usage](#basic-usage)
* [Core API](#core-api)
  * [Starting, stopping, pausing, resuming and running GC](#starting-stopping-pausing-resuming-and-running-gc)
  * [Memory allocation and deallocation](#memory-allocation-and-deallocation)
  * [Helper functions](#helper-functions)
* [Basic Concepts](#basic-concepts)
  * [Hashmap implementation and private API](#hashmap-implementation-and-private-api)
  * [Unused memory: the reachability definition](#unused-memory-the-reachability-definition)
  * [Mark-and-sweep: basic algo](#mark-and-sweep-basic-algo)
  * [Hash map implementation](#hash-map-implementation)
  * [Finding roots](#finding-roots)
  * [Dumping registers on the stack](#dumping-registers-on-the-stack)
  * [Depth-first recursive marking](#depth-first-recursive-marking)



## Documentation Overview

* Read the [quickstart](#quickstart) below to see how to get started quickly
* The [concepts](#concepts) section describes the basic concepts and design
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
    // look ma, no free!
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

## Core API

This describes the core API, see `gc.h` for more details and the low-level API.

### Starting, stopping, pausing, resuming and running GC

In order to initialize and start garbage collection, use the `gc_start()`
function and pass a *bottom-of-stack* address:

```c
void gc_start(GarbageCollector* gc, void* bos);
```

The bottom-of-stack parameter `bos` needs to point to a stack-allocated
variable and marks the low end of the stack from where [root
finding](#root-finding) (scanning) starts. 

Garbage collection can be stopped, paused and resumed with

```c
void gc_stop(GarbageCollector* gc);
void gc_pause(GarbageCollector* gc);
void gc_resume(GarbageCollector* gc);
```

and manual garbage collection can be triggered with

```c
size_t gc_run(GarbageCollector* gc);
```

### Memory allocation and deallocation

`gc` supports `malloc()`, `calloc()`and `realloc()`-style memory allocation.
The respective funtion signatures mimick the POSIX functions (with the
exception that we need to pass the garbage collector along as the first
argument):

```c
void* gc_malloc(GarbageCollector* gc, size_t size);
void* gc_calloc(GarbageCollector* gc, size_t count, size_t size);
void* gc_realloc(GarbageCollector* gc, void* ptr, size_t size);
```

It is possible to pass a pointer to a desctructor function through the
extended interface:

```c
void* dtor(void* obj) {
   // do some cleanup work
   obj->parent->deregister();
   obj->db->disconnect()
   ...
   // no need to free obj
}
...
SomeObject* obj = gc_malloc_ext(gc, sizeof(SomeObject), dtor);
...
``` 

It is possible to trigger explicit memory deallocation using 

```c
void gc_free(GarbageCollector* gc, void* ptr);
```

Calling `gc_free()` is guaranteed to (a) call the destructor on the object
pointed to by `ptr` and (b) to free the memory that `ptr` points to
irrespective of the current scheduling for garbage collection and will also
work if GC has been paused using `gc_pause()` above.

### Helper functions

`gc` also offers a `strdup()` implementation that returns a garbage-collected
copy:

```c
char* gc_strdup (GarbageCollector* gc, const char* s);
```


## Basic Concepts

The fundamental idea behind garbage collection is to automate the memory
allocation/deallocation cycle. This is accomplished by keeping track of all
allocated memory and periodically triggering deallocation for memory that is
still allocated but [unused](#unused-memory).

Many advanced garbage collectors also implement their own approach to memory
allocation (i.e. replace `malloc()`). This often enables them to layout memory
in a more space-efficient manner or for faster access but comes at the price of
architecture-specific implementations and increased complexity. `gc` sidesteps
these issues by falling back on the POSIX `malloc()` implementation and keeping
memory management and garbage collection metadata separate. This makes `gc`
much simpler to understand but, of course, also less space- and time-efficient
than more optimized approaches.

The core data structure inside `gc` is a hash map that maps the address of
allocated memory to the garbage collection metadata of that memory:

The items in the hash map are allocations, modeles with the `Allocation`
`struct`:

```c
typedef struct Allocation {
    void* ptr;                // mem pointer
    size_t size;              // allocated size in bytes
    char tag;                 // the tag for mark-and-sweep
    void (*dtor)(void*);      // destructor
    struct Allocation* next;  // separate chaining
} Allocation;
```

Each `Allocation` instance holds a pointer to the allocated memory, the size of
the allocated memory at that location, a tag for mark-and-sweep (see below), an
optional pointer to the destructor function and a pointer to the next
`Allocation` instance (for separate chaining, see below).

The allocations are collected in an `AllocationMap` 

```c
typedef struct AllocationMap {
    size_t capacity;
    size_t min_capacity;
    double downsize_factor;
    double upsize_factor;
    double sweep_factor;
    size_t sweep_limit;
    size_t size;
    Allocation** allocs;
} AllocationMap;
```

that, together with a set of `static` functions inside `gc.c`, provides hash
map semantics for the implementation of the public API.

The `AllocationMap` is the central data structure in the `GarbageCollector`
struct which is part of the public API:

```c
typedef struct GarbageCollector {
    struct AllocationMap* allocs;
    bool paused;
    void *bos;
    size_t min_size;
} GarbageCollector;
```

With the basic data structures in place, any `gc_*alloc()` memory allocation
request is a two-step procedure: first, allocate the memory through system (i.e.
standard `malloc()`) functionality and second, add or update the associated
metadata to the hash map.

For `gc_free()`, use the pointer to locate the metadata in the hash map,
determine if the deallocation requires a destructor call, call if required,
free the managed memory and delete the metadata entry from the hash map.

In summary, the above data structures and the associated interfaces given us
the ability to manage the metadata required to build a garbage collector.




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

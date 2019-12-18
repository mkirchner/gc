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

* [Table of contents](#table-of-contents)
* [Documentation Overview](#documentation-overview)
* [Quickstart](#quickstart)
  * [Download and test](#download-and-test)
  * [Basic usage](#basic-usage)
* [Core API](#core-api)
  * [Starting, stopping, pausing, resuming and running GC](#starting-stopping-pausing-resuming-and-running-gc)
  * [Memory allocation and deallocation](#memory-allocation-and-deallocation)
  * [Helper functions](#helper-functions)
* [Basic Concepts](#basic-concepts)
  * [Data Structures](#data-structures)
  * [Garbage collection](#garbage-collection)
  * [Reachability](#reachability)
  * [The Mark-and-Sweep Algorithm](#the-mark-and-sweep-algorithm)
  * [Finding roots](#finding-roots)
  * [Depth-first recursive marking](#depth-first-recursive-marking)
  * [Dumping registers on the stack](#dumping-registers-on-the-stack)


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

Calling `gc_free()` is guaranteed to (a) finalize/destruct on the object
pointed to by `ptr` if applicable and (b) to free the memory that `ptr` points to
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

### Data Structures

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


### Garbage collection

The above data structures and the associated interfaces given us
the ability to manage the metadata required to build a garbage collector.

`gc` triggers collection under two circumstances: (a) when any of the calls to
the system allocation fail (in the hope to deallocate sufficient memory to
fulfill the current request); and (b) when the number of entries in the hash
map passes a dynamically adjusted high water mark.

If either of these cases occurs, `gc` stops the world and starts a
mark-and-sweep garbage collection run over all current allocations. This
functionality is implemented in the `gc_run()` function which is part of the
public API and delegates all work to the `gc_mark()` and `gc_sweep()` functions
that are part of the private API.

`gc_mark()` has the task of [finding roots](#finding-roots) and tagging all
known allocations that are referenced from a root (or from an allocation that
is referenced from a root, i.e. transitively) as "used". Once the marking of
is completed, `gc_sweep()` iterates over all known allocations and
deallocates all unused (i.e. unmarked) allocations, returns to `gc_run()` and
the world continues to run.


### Reachability

`gc` will keep memory allocations that are *reachable* and collect everything
else. An allocation is considered reachable if any of the following is true:

1. There is a pointer on the stack that points to the allocation content.
   The pointer must reside in a stack frame that is at least as deep in the call
   stack as the bottom-of-stack variable passed to `gc_start()` (i.e. `bos` is
   the smallest stack address considered during the mark phase).
2. There is a pointer inside `gc_*alloc()`-allocated content that points to the
   allocation content.
3. The allocation is tagged with GC_TAG_ROOT.


### The Mark-and-Sweep Algorithm

The naïve mark-and-sweep algorithm runs in two stages. First, in a *mark*
stage, the algorithm detects all *roots* and from there marks all reachable
allocations. Second, in the *sweep* stage, the algorithm iterates over all
known allocations and deletes the unmarked (i.e. unreachable) allocations.

### Finding roots

`gc` attempts to detect roots in the stack (starting from the bottom-of-stack
pointer `bos` that is passed to `gc_start()`) and the registers (by [dumping them
on the stack](#dumping-registers-on-the-stack) prior to the mark phase). It
also supports heap-allocated objects that have the `GC_TAG_ROOT` tag set.

Marking an allocation consists of (1) setting the `tag` field in an `Allocation`
object to `GC_TAG_MARK` and (2) scanning the allocated memory for pointers to
known allocations, recursively repeating the process.

### Depth-first recursive marking

Marking allocations is implemented as a simple, recursive depth-first search with `char` size
increments (since there is no guarantee for any stack alignment):

```c
void gc_mark_alloc(GarbageCollector* gc, void* ptr)
{
    Allocation* alloc = gc_allocation_map_get(gc->allocs, ptr);
    if (alloc && !(alloc->tag & GC_TAG_MARK)) {
        alloc->tag |= GC_TAG_MARK;
        for (char* p = (char*) alloc->ptr;
             p < (char*) alloc->ptr + alloc->size;
             ++p) {
            gc_mark_alloc(gc, *(void**)p);
        }
    }
}
```

In `gc.c`, `gc_mark()` starts the marking process by marking the
known roots on the stack via a call to `gc_mark_roots()`. To mark the roots we
do one full pass through all known allocations. We then proceed to dump the
registers on the stack.


### Dumping registers on the stack

Dumping registers on the stack is implemented using `setjmp()`, which stores
them in a `jmp_buf` variable right before we mark the stack. The detour using
the `volatile` function pointer `_mark_stack` to the `gc_mark_stack()` function
is necessary to avoid the inlining of the call to `gc_mark_stack()`.

```c
/* Dump registers onto stack and scan the stack */
void (*volatile _mark_stack)(GarbageCollector*) = gc_mark_stack;
jmp_buf ctx;
memset(&ctx, 0, sizeof(jmp_buf));
setjmp(ctx);
_mark_stack(gc);
```


[naive_mas]: https://en.wikipedia.org/wiki/Tracing_garbage_collection#Naïve_mark-and-sweep
[boehm]: https://www.hboehm.info/gc/ 
[stutter]: https://github.com/mkirchner/stutter
[tgc]: https://github.com/orangeduck/tgc
[garbage_collection_handbook]: http://gchandbook.org

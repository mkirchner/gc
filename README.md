# gc
Zero-dependency garbage collection for C.

## Quickstart

### Download and test

    $ git clone git@github.com:mkirchner/gc.git
    $ cd gc
    $ make test

### Use

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
    int bos;
    gc = gc_start(gc, &bos);
    ...
    some_fun();
    ...
    gc_stop(gc);
    return 0;
}
```

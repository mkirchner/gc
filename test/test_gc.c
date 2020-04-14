#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include "minunit.h"

#include "../src/gc.c"

#define UNUSED(x) (void)(x)

static size_t DTOR_COUNT = 0;

static char* test_primes()
{
    /*
     * Test a few known cases.
     */
    mu_assert(!is_prime(0), "Prime test failure for 0");
    mu_assert(!is_prime(1), "Prime test failure for 1");
    mu_assert(is_prime(2), "Prime test failure for 2");
    mu_assert(is_prime(3), "Prime test failure for 3");
    mu_assert(!is_prime(12742382), "Prime test failure for 12742382");
    mu_assert(is_prime(611953), "Prime test failure for 611953");
    mu_assert(is_prime(479001599), "Prime test failure for 479001599");
    return 0;
}

void dtor(void* ptr)
{
    UNUSED(ptr);
    DTOR_COUNT++;
}

static char* test_gc_allocation_new_delete()
{
    int* ptr = (int *) malloc(sizeof(int));
    Allocation* a = gc_allocation_new(ptr, sizeof(int), dtor);
    mu_assert(a != NULL, "Allocation should return non-NULL");
    mu_assert(a->ptr == ptr, "Allocation should contain original pointer");
    mu_assert(a->size == sizeof(int), "Size of mem pointed to should not change");
    mu_assert(a->tag == GC_TAG_NONE, "Annotation should initially be untagged");
    mu_assert(a->dtor == dtor, "Destructor pointer should not change");
    mu_assert(a->next == NULL, "Annotation should initilally be unlinked");
    gc_allocation_delete(a);
    free(ptr);
    return NULL;
}


static char* test_gc_allocation_map_new_delete()
{
    /* Standard invocation */
    AllocationMap* am = gc_allocation_map_new(8, 16, 0.5, 0.2, 0.8);
    mu_assert(am->min_capacity == 11, "True min capacity should be next prime");
    mu_assert(am->capacity == 17, "True capacity should be next prime");
    mu_assert(am->size == 0, "Allocation map should be initialized to empty");
    mu_assert(am->sweep_limit == 8, "Incorrect sweep limit calculation");
    mu_assert(am->downsize_factor == 0.2, "Downsize factor should not change");
    mu_assert(am->upsize_factor == 0.8, "Upsize factor should not change");
    mu_assert(am->allocs != NULL, "Allocation map must not have a NULL pointer");
    gc_allocation_map_delete(am);

    /* Enforce min sizes */
    am = gc_allocation_map_new(8, 4, 0.5, 0.2, 0.8);
    mu_assert(am->min_capacity == 11, "True min capacity should be next prime");
    mu_assert(am->capacity == 11, "True capacity should be next prime");
    mu_assert(am->size == 0, "Allocation map should be initialized to empty");
    mu_assert(am->sweep_limit == 5, "Incorrect sweep limit calculation");
    mu_assert(am->downsize_factor == 0.2, "Downsize factor should not change");
    mu_assert(am->upsize_factor == 0.8, "Upsize factor should not change");
    mu_assert(am->allocs != NULL, "Allocation map must not have a NULL pointer");
    gc_allocation_map_delete(am);

    return NULL;
}


static char* test_gc_allocation_map_basic_get()
{
    AllocationMap* am = gc_allocation_map_new(8, 16, 0.5, 0.2, 0.8);
    Allocation* b, *c;
    /* Ask for something that does not exist */
    int* five = (int *) malloc(sizeof(int));
    Allocation* a = gc_allocation_map_get(am, five);
    mu_assert(a == NULL, "Empty allocation map must not contain any allocations");

    /* Create an entry and query it */
    *five = 5;
    a = gc_allocation_map_put(am, five, sizeof(int), NULL);
    mu_assert(a != NULL, "Result of PUT on allocation map must be non-NULL");
    mu_assert(am->size == 1, "Expect size of one-element map to be one");
    mu_assert(am->allocs != NULL, "AllocationMap must hold list of allocations");
    b = gc_allocation_map_get(am, five);
    mu_assert(a == b, "Get should return the same result as put");
    mu_assert(a->ptr == b->ptr, "Pointers must not change between calls");
    mu_assert(b->ptr == five, "Get result should equal original pointer");

    /* Update the entry  and query */
    a = gc_allocation_map_put(am, five, sizeof(int), dtor);
    mu_assert(am->size == 1, "Expect size of one-element map to be one");
    mu_assert(a->dtor == dtor, "Setting the dtor should set the dtor");
    b = gc_allocation_map_get(am, five);
    mu_assert(b->dtor == dtor, "Failed to persist the dtor update");

    /* Delete the entry */
    gc_allocation_map_remove(am, five, true);
    mu_assert(am->size == 0, "After removing last item, map should be empty");
    c = gc_allocation_map_get(am, five);
    mu_assert(c == NULL, "Empty allocation map must not contain any allocations");

    gc_allocation_map_delete(am);
    free(five);
    return NULL;
}


static char* test_gc_allocation_map_put_get_remove()
{
    size_t i;
    AllocationMap* am;
    Allocation* a;
    /* Create a few data pointers */
    int** ints = (int **) malloc(64*sizeof(int*));
    for (i=0; i<64; ++i) {
        ints[i] = (int *) malloc(sizeof(int));
    }

    /* Enforce separate chaining by disallowing up/downsizing.
     * The pigeonhole principle then states that we need to have at least one
     * entry in the hash map that has a separate chain with len > 1
     */
    am = gc_allocation_map_new(32, 32, DBL_MAX, 0.0, DBL_MAX);
    for (i=0; i<64; ++i) {
        a = gc_allocation_map_put(am, ints[i], sizeof(int), NULL);
    }
    mu_assert(am->size == 64, "Maps w/ 64 elements should have size 64");
    /* Now update all of them with a new dtor */
    for (i=0; i<64; ++i) {
        a = gc_allocation_map_put(am, ints[i], sizeof(int), dtor);
    }
    mu_assert(am->size == 64, "Maps w/ 64 elements should have size 64");
    /* Now delete all of them again */
    for (i=0; i<64; ++i) {
        gc_allocation_map_remove(am, ints[i], true);
    }
    mu_assert(am->size == 0, "Empty map must have size 0");
    /* And delete the entire map */
    gc_allocation_map_delete(am);

    /* Clean up the data pointers */
    for (i=0; i<64; ++i) {
        free(ints[i]);
    }
    free(ints);

    return NULL;
}

static char* test_gc_allocation_map_cleanup()
{
    GarbageCollector gc_ = { 0 };
    void *bos;
    int** ptrs;
    size_t j, i;
    /* Make sure that the entries in the allocation map get reset
     * to NULL when we delete things. This is required for the
     * chunk != NULL checks when iterating over the items in the hash map.
     */
    DTOR_COUNT = 0;
    bos = __builtin_frame_address(0);
    gc_start_ext(&gc_, bos, 32, 32, 0.0, DBL_MAX, DBL_MAX);

    /* run a few alloc/free cycles */
    ptrs = (int **) gc_malloc_ext(&gc_, 64*sizeof(int*), dtor);
    for (j=0; j<8; ++j) {
        for (i=0; i<64; ++i) {
            ptrs[i] = (int *) gc_malloc(&gc_, i*sizeof(int));
        }
        for (i=0; i<64; ++i) {
            gc_free(&gc_, ptrs[i]);
        }
    }
    gc_free(&gc_, ptrs);
    mu_assert(DTOR_COUNT == 1, "Failed to call destructor for array");
    DTOR_COUNT = 0;

    /* now make sure that all allocation entries are NULL */
    for (i=0; i<gc_.allocs->capacity; ++i) {
        mu_assert(gc_.allocs->allocs[i] == NULL, "Deleted allocs should be reset to NULL");
    }
    gc_stop(&gc_);
    return NULL;
}


static char* test_gc_mark_stack()
{
    GarbageCollector gc_={ 0 };
    void *bos;
    Allocation* a;
    int** five_ptr;
    size_t i;
    Allocation* unmarked_alloc;

    bos = __builtin_frame_address(0);
    gc_start_ext(&gc_, bos, 32, 32, 0.0, DBL_MAX, DBL_MAX);

    gc_pause(&gc_);

    /* Part 1: Create an object on the heap, reference from the stack,
     * and validate that it gets marked. */
    five_ptr = (int **) gc_calloc(&gc_, 2, sizeof(int*));
    gc_mark_stack(&gc_);
    a = gc_allocation_map_get(gc_.allocs, five_ptr);
    mu_assert(a->tag & GC_TAG_MARK, "Heap allocation referenced from stack should be tagged");

    /* manually reset the tags */
    a->tag = GC_TAG_NONE;

    /* Part 2: Add dependent allocations and check if these allocations
     * get marked properly*/
    five_ptr[0] = (int *) gc_malloc(&gc_, sizeof(int));
    *five_ptr[0] = 5;
    five_ptr[1] = (int *) gc_malloc(&gc_, sizeof(int));
    *five_ptr[1] = 5;
    gc_mark_stack(&gc_);
    a = gc_allocation_map_get(gc_.allocs, five_ptr);
    mu_assert(a->tag & GC_TAG_MARK, "Referenced heap allocation should be tagged");
    for (i=0; i<2; ++i) {
        a = gc_allocation_map_get(gc_.allocs, five_ptr[i]);
        mu_assert(a->tag & GC_TAG_MARK, "Dependent heap allocs should be tagged");
    }

    /* Clean up the tags manually */
    a = gc_allocation_map_get(gc_.allocs, five_ptr);
    a->tag = GC_TAG_NONE;
    for (i=0; i<2; ++i) {
        a = gc_allocation_map_get(gc_.allocs, five_ptr[i]);
        a->tag = GC_TAG_NONE;
    }

    /* Part3: Now delete the pointer to five_ptr[1] which should
     * leave the allocation for five_ptr[1] unmarked. */
    unmarked_alloc = gc_allocation_map_get(gc_.allocs, five_ptr[1]);
    five_ptr[1] = NULL;
    gc_mark_stack(&gc_);
    a = gc_allocation_map_get(gc_.allocs, five_ptr);
    mu_assert(a->tag & GC_TAG_MARK, "Referenced heap allocation should be tagged");
    a = gc_allocation_map_get(gc_.allocs, five_ptr[0]);
    mu_assert(a->tag & GC_TAG_MARK, "Referenced alloc should be tagged");
    mu_assert(unmarked_alloc->tag == GC_TAG_NONE, "Unreferenced alloc should not be tagged");

    /* Clean up the tags manually, again */
    a = gc_allocation_map_get(gc_.allocs, five_ptr[0]);
    a->tag = GC_TAG_NONE;
    a = gc_allocation_map_get(gc_.allocs, five_ptr);
    a->tag = GC_TAG_NONE;

    gc_stop(&gc_);
    return NULL;
}


static char* test_gc_basic_alloc_free()
{
    GarbageCollector gc_ = { 0 };
    void *bos;
    int** ints;
    Allocation* a;
    size_t i, total, n;
    /* Create an array of pointers to an int. Then delete the pointer to
     * the containing array and check if all the contained allocs are garbage
     * collected.
     */
    DTOR_COUNT = 0;

    bos = __builtin_frame_address(0);
    gc_start_ext(&gc_, bos, 32, 32, 0.0, DBL_MAX, DBL_MAX);

    ints = (int **) gc_calloc(&gc_, 16, sizeof(int*));
    a = gc_allocation_map_get(gc_.allocs, ints);
    mu_assert(a->size == 16*sizeof(int*), "Wrong allocation size");

    for (i=0; i<16; ++i) {
        ints[i] = (int *) gc_malloc_ext(&gc_, sizeof(int), dtor);
        *ints[i] = 42;
    }
    mu_assert(gc_.allocs->size == 17, "Wrong allocation map size");

    /* Test that all managed allocations get tagged if the root is present */
    gc_mark(&gc_);
    for (i=0; i<gc_.allocs->capacity; ++i) {
        Allocation* chunk = gc_.allocs->allocs[i];
        while (chunk) {
            mu_assert(chunk->tag & GC_TAG_MARK, "Referenced allocs should be marked");
            // reset for next test
            chunk->tag = GC_TAG_NONE;
            chunk = chunk->next;
        }
    }

    /* Now drop the root allocation */
    ints = NULL;
    gc_mark(&gc_);

    /* Check that none of the allocations get tagged */
    total = 0;
    for (i=0; i<gc_.allocs->capacity; ++i) {
        Allocation* chunk = gc_.allocs->allocs[i];
        while (chunk) {
            mu_assert(!(chunk->tag & GC_TAG_MARK), "Unreferenced allocs should not be marked");
            total += chunk->size;
            chunk = chunk->next;
        }
    }
    mu_assert(total == 16 * sizeof(int) + 16 * sizeof(int*),
              "Expected number of managed bytes is off");

    n = gc_sweep(&gc_);
    mu_assert(n == total, "Wrong number of collected bytes");
    mu_assert(DTOR_COUNT == 16, "Failed to call destructor");
    DTOR_COUNT = 0;
    gc_stop(&gc_);
    return NULL;
}

static void _create_static_allocs(GarbageCollector* gc,
                                  size_t count,
                                  size_t size)
{
    size_t i;
    for (i=0; i<count; ++i) {
        void* p = gc_malloc_static(gc, size, dtor);
        memset(p, 0, size);
    }
}

static char* test_gc_static_allocation()
{
    GarbageCollector gc_ = { 0 };
    void *bos;
    size_t N;
    size_t collected, total, n, i;
    DTOR_COUNT = 0;
    bos = __builtin_frame_address(0);
    gc_start(&gc_, bos);
    /* allocate a bunch of static vars in a deeper stack frame */
    N = 256;
    _create_static_allocs(&gc_, N, 512);
    /* make sure they are not garbage collected */
    collected = gc_run(&gc_);
    mu_assert(collected == 0, "Static objects should not be collected");
    /* remove the root tag from the roots on the heap */
    gc_unroot_roots(&gc_);
    /* run the mark phase */
    gc_mark_roots(&gc_);
    /* Check that none of the allocations were tagged. */
    total = 0;
    n = 0;
    for (i=0; i<gc_.allocs->capacity; ++i) {
        Allocation* chunk = gc_.allocs->allocs[i];
        while (chunk) {
            mu_assert(!(chunk->tag & GC_TAG_MARK), "Marked an unused alloc");
            mu_assert(!(chunk->tag & GC_TAG_ROOT), "Unrooting failed");
            total += chunk->size;
            n++;
            chunk = chunk->next;
        }
    }
    mu_assert(n == N, "Expected number of allocations is off");
    mu_assert(total == N*512, "Expected number of managed bytes is off");
    /* make sure we collect everything */
    collected = gc_sweep(&gc_);
    mu_assert(collected == N*512, "Unexpected number of bytes");
    mu_assert(DTOR_COUNT == N, "Failed to call destructor");
    DTOR_COUNT = 0;
    gc_stop(&gc_);
    return NULL;
}

static char* test_gc_realloc()
{
    GarbageCollector gc_;
    void *bos = __builtin_frame_address(0);
    gc_start(&gc_, bos);

    /* manually allocate some memory */
    {
        void *unmarked = malloc(sizeof(char));
        void *re_unmarked = gc_realloc(&gc_, unmarked, sizeof(char) * 2);
        mu_assert(!re_unmarked, "GC should not realloc pointers unknown to it");
        free(unmarked);
    }

    /* reallocing NULL pointer */
    {
        void *unmarked = NULL;
        void *re_marked = gc_realloc(&gc_, unmarked, sizeof(char) * 42);
        mu_assert(re_marked, "GC should not realloc NULL pointers");
        Allocation* a = gc_allocation_map_get(gc_.allocs, re_marked);
        mu_assert(a->size == 42, "Wrong allocation size");
    }

    /* realloc a valid pointer with same size to enforce same pointer is used*/
    {
        int** ints = gc_calloc(&gc_, 16, sizeof(int*));
        ints = gc_realloc(&gc_, ints, 16*sizeof(int*));
        Allocation* a = gc_allocation_map_get(gc_.allocs, ints);
        mu_assert(a->size == 16*sizeof(int*), "Wrong allocation size");
    }

    /* realloc with size greater than before */
    {
        int** ints = gc_calloc(&gc_, 16, sizeof(int*));
        ints = gc_realloc(&gc_, ints, 42*sizeof(int*));
        Allocation* a = gc_allocation_map_get(gc_.allocs, ints);
        mu_assert(a->size == 42*sizeof(int*), "Wrong allocation size");
    }

    gc_stop(&gc_);
    return NULL;
}

static void _create_allocs(GarbageCollector* gc,
                           size_t count,
                           size_t size)
{
    size_t i;
    for (i=0; i<count; ++i) {
        gc_malloc(gc, size);
    }
}

static char* test_gc_pause_resume()
{
    GarbageCollector gc_ = { 0 };
    void *bos;
    size_t N, collected;
    bos = __builtin_frame_address(0);
    gc_start(&gc_, bos);
    /* allocate a bunch of vars in a deeper stack frame */
    N = 32;
    _create_allocs(&gc_, N, 8);
    /* make sure they are garbage collected after a  pause->resume cycle */
    gc_pause(&gc_);
    mu_assert(gc_.paused, "GC should be paused after pausing");
    gc_resume(&gc_);

    /* Avoid dumping the registers on the stack to make test less flaky */
    gc_mark_roots(&gc_);
    gc_mark_stack(&gc_);
    collected = gc_sweep(&gc_);

    mu_assert(collected == N*8, "Unexpected number of collected bytes in pause/resume");
    gc_stop(&gc_);
    return NULL;
}

static char* duplicate_string(GarbageCollector* gc, char* str)
{
    char* copy = (char*) gc_strdup(gc, str);
    mu_assert(strncmp(str, copy, 16) == 0, "Strings should be equal");
    return NULL;
}

char* test_gc_strdup()
{
    GarbageCollector gc_ = { 0 };
    void *bos;
    char* str, *error;
    size_t collected;
    bos = __builtin_frame_address(0);
    gc_start(&gc_, bos);
    str = "This is a string";
    error = duplicate_string(&gc_, str);
    mu_assert(error == NULL, "Duplication failed"); // cascade minunit tests
    collected = gc_run(&gc_);
    mu_assert(collected == 17, "Unexpected number of collected bytes in strdup");
    gc_stop(&gc_);
    return NULL;
}

/*
 * Test runner
 */

int tests_run = 0;

static char* test_suite()
{
    printf("---=[ GC tests\n");
    mu_run_test(test_gc_allocation_new_delete);
    mu_run_test(test_gc_allocation_map_new_delete);
    mu_run_test(test_gc_allocation_map_basic_get);
    mu_run_test(test_gc_allocation_map_put_get_remove);
    mu_run_test(test_gc_mark_stack);
    mu_run_test(test_gc_basic_alloc_free);
    mu_run_test(test_gc_allocation_map_cleanup);
    mu_run_test(test_gc_static_allocation);
    mu_run_test(test_primes);
    mu_run_test(test_gc_realloc);
    mu_run_test(test_gc_pause_resume);
    mu_run_test(test_gc_strdup);
    return 0;
}

int main()
{
    char *result;
    result = test_suite();
    if (result) {
        printf("%s\n", result);
    } else {
        printf("ALL TESTS PASSED\n");
    }
    printf("Tests run: %d\n", tests_run);
    return result != 0;
}


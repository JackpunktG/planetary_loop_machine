#ifndef ARENA_MEMORY_H
#define ARENA_MEMORY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define ARENA_BLOCK_SIZE  (1024 * 1024)   //size of block currently 1MB

/* Dynamic size Arena Block Allocation */ //if over the defualt size will make a block for that size
typedef struct ArenaBlock
{
    uint8_t* memory;            //raw memory -> 1 per byte
    size_t size;                //total block size
    size_t used;                //how much of the block is used
    struct ArenaBlock* next;    //link to next block if we need more space
} ArenaBlock;

/* Free List Structure for reusing memory - project specific
Need to implement populating the free list with the sizes of chuck to be reused
Use a size flag in your struct to be told how much space they are taking up and
when they are ready to be reused how much space is avalible even if they didn't
take up the entire space*/
typedef struct
{

    void** memory;      //pointer to each chunk
    size_t* sizes;         //size of each chunk
    size_t count;
    size_t maxCount;  //starts with 100
} FreeList;

/* Arena Structure */
typedef struct
{
    ArenaBlock* current;        //currently allocating block
    ArenaBlock* first;          //start of our block
    size_t totalAllocated;      //Total byte allocated
    size_t defualtBlockSize;    // size of new block
    size_t alignment;           //number of bits the Arena should be aligned to
    FreeList* freeList;         //pointer to the free list for deallocation and reuse of memory - project specific
} Arena;
//freeList == NULL when using free list is false, to allow own implementation of a free list if needed
Arena* arena_init(size_t defualtBlockSize, size_t alignment, bool useFreeList);
void arena_destroy(Arena* arena);
//the sizeAlloc send back, sends the size of the allocated space to keep track of for free list, send NULL for no sendback
void* arena_alloc(Arena* arena, size_t size, size_t* sizeAllocSendBack);
void arena_reset(Arena* arena); //just restting all the allocated counters to zero and ptr to the start for the blocks
//to realloc, old_size is needed to add back to free list properly
void* arena_realloc(Arena* arena, void* old_ptr, size_t old_size, size_t new_size, size_t* sizeAllocSendBack);
//adds a pointer and size to the free list for reuse later
void arena_free_list_add(Arena* arena, void* ptr, size_t size);
#endif // ARENA_MEMORY_H

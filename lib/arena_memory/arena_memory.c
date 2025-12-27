#include "arena_memory.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>


//static mean its only for internal use, and can't be accidentially assess by somewhere else the same function being declared
static ArenaBlock* arena_add_block(Arena *arena, size_t minimumSize)
{
    //checking if size fits in one block
    size_t blockSize = minimumSize > arena->defualtBlockSize ? minimumSize : arena->defualtBlockSize;

    // allocate block struct
    ArenaBlock* block = malloc(sizeof(ArenaBlock));
    if(!block)
    {
        printf("ERROR - could not allocate block struct\n");
        return NULL;
    }
    // allocating the actual memory
    block->memory = malloc(blockSize);
    if(!block->memory)
    {
        free(block);
        printf("ERROR - could not allocate block memory\n");
        return NULL;
    }

    //init the block members
    block->size = blockSize;
    block->used = 0;
    block->next = NULL;

    //link into arena
    if (arena->current)   //if its not the first block, linking it to the last block before allocating it to current
        arena->current->next = block;
    else
        arena->first = block;

    arena->current = block;

    return block;
}

//alows for different alignment
static size_t align_to(size_t size, size_t alignment)
{
    return (size + (alignment -1)) & ~(alignment -1);
}

Arena* arena_init(size_t blockSize, size_t alignment, bool fl)
{
    //checking for 0 and must be a power of two
    if (alignment == 0 || (alignment & (alignment -1)))
    {
        printf("ERROR - alignment cannot be 0 and must be a power of two\n");
        return NULL;
    }

    if (blockSize != align_to(blockSize, alignment))
    {
        printf("WARNING - Mismatch with defualt Block Size and alignment\n");
        blockSize = align_to(blockSize, alignment);
        printf("New defualtBlockSize based on your alignment: %lu\n", blockSize);
    }

    //Init arena controller struct
    Arena* arena = malloc(sizeof(Arena));
    if (!arena)
    {
        printf("ERROR - arena_init malloc failed\n");
        return NULL;
    }


    //init fields
    arena->defualtBlockSize = blockSize;
    arena->totalAllocated = 0;
    arena->alignment = alignment;
    arena->current = NULL;
    arena->first = NULL;

    // Create first block
    if(!arena_add_block(arena, blockSize))
    {
        free(arena);
        printf("ERROR - Failed to create first block");
        return NULL;
    }

    //init free list
    if (!fl)
    {
        arena->freeList = NULL;

    }
    else
    {
        FreeList* freeList = arena_alloc(arena, sizeof(FreeList), NULL);
        if (!freeList)
        {
            arena_destroy(arena);
            printf("ERROR - Failed to create free list\n");
            return NULL;
        }
        freeList->memory = arena_alloc(arena, sizeof(void*) * 100, NULL);
        for (int i = 0; i < 100; i++)
            freeList->memory[i] = NULL;
        if (!freeList->memory)
        {
            arena_destroy(arena);
            printf("ERROR - Failed to create free list memory\n");
            return NULL;
        }
        freeList->sizes = arena_alloc(arena, sizeof(size_t) * 100, NULL);
        for (int i = 0; i < 100; i++)
            freeList->sizes[i] = 0;
        if (!freeList->sizes)
        {
            arena_destroy(arena);
            printf("ERROR - Failed to create free list sizes\n");
            return NULL;
        }
        freeList->count = 0;
        freeList->maxCount = 100;
        arena->freeList = freeList;
    }

    return arena;
}



void* arena_alloc(Arena* arena, size_t size, size_t* sizeAlloc)
{
    if (!arena || !size)
    {
        printf("arena? size: %p %lu\n", (void*)arena, size);
        printf("ERROR - arena or size are NULL\n");
        return NULL;
    }

    if (arena->freeList != NULL && arena->freeList->count > 0)
    {
        FreeList* fl = arena->freeList;
        for (int i = 0; i < fl->count; ++i)
        {
            if (fl->sizes[i] >= size)
            {
                if (sizeAlloc != NULL) *sizeAlloc = fl->sizes[i];
                return fl->memory[i];
            }
        }

    }

    //align size to next mulitiple of 8 bytes
    size = align_to(size, arena->alignment);

    //check if current block has enough space
    if (!arena->current || arena->current->used + size > arena->current->size)
    {
        //new block if size is not enough
        if(!arena_add_block(arena, size))
            return NULL;
    }


    //get the pointer to the space
    void* ptr = arena->current->memory + arena->current->used;


    //## Perhaps some way to verify alignment and fix if not aligned properly??


    //updated current used and total used
    arena->current->used += size;
    arena->totalAllocated += size;

    if (sizeAlloc != NULL) *sizeAlloc = size;

    return ptr;
}

void arena_reset(Arena* arena)
{
    if (!arena)
    {
        printf("ERROR - arena is already NULL before the reset\n");
        return;
    }

    //Marking all blocks as empty
    ArenaBlock* block = arena->first;
    while(block)
    {
        block->used = 0;
        block = block->next;
    }

    arena->current = arena->first;
    arena->totalAllocated = 0;
}

void* arena_realloc(Arena* arena, void* oldPtr, size_t oldSize, size_t newSize, size_t* sizeAllocSendBack)
{
    if (!arena || !oldPtr || !oldSize || !newSize)
    {
        printf("ERROR - arena, oldPtr, oldSize or newSize are NULL\n");
        return NULL;
    }

    //allocate new space
    void* new_ptr = arena_alloc(arena, newSize, sizeAllocSendBack);
    if (!new_ptr)
    {
        printf("ERROR - arena_realloc failed to allocate new memory\n");
        return NULL;
    }

    //copy old data to new space
    size_t copy_size = oldSize <= newSize ? oldSize : newSize;
    memcpy(new_ptr, oldPtr, copy_size);

    if (arena->freeList != NULL && arena->freeList->count > arena->freeList->maxCount)
    {
        arena->freeList->memory[arena->freeList->count] = oldPtr;
        arena->freeList->sizes[arena->freeList->count] = oldSize;
        arena->freeList->count += 1;
        //Check and grow free list if needed
    }

    if (sizeAllocSendBack != NULL) *sizeAllocSendBack = newSize;

    return new_ptr;
}

void arena_free_list_add(Arena* arena, void* ptr, size_t size)
{
    if (!arena || !ptr || !size)
    {
        printf("ERROR - arena, ptr or size are NULL\n");
        return;
    }

    if (arena->freeList != NULL && arena->freeList->count < arena->freeList->maxCount)
    {
        arena->freeList->memory[arena->freeList->count] = ptr;
        arena->freeList->sizes[arena->freeList->count] = size;
        arena->freeList->count += 1;
        //Check and grow free list if needed
    }
}


void arena_destroy(Arena* arena)
{
    if (!arena)
    {
        printf("ERROR - arena is already NULL before the destroy\n");
        return;
    }
    if (arena->freeList)
        arena->freeList = NULL;

    //free all blocks
    ArenaBlock* block = arena->first;
    while(block)
    {
        ArenaBlock* next = block->next;
        free(block->memory);
        free(block);
        block = next;
    }

    free(arena);
}

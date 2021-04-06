#include <string.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>

/* The standard allocator interface from stdlib.h.  These are the
 * functions you must implement, more information on each function is
 * found below. They are declared here in case you want to use one
 * function in the implementation of another. */
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);

/* When requesting memory from the OS using sbrk(), request it in
 * increments of CHUNK_SIZE. */
#define __CHUNK_SIZE (1<<12)
#define __CHUNK_NUM 8
#define __OFFICE_SIZE 32

#define __ALLOC(block)	block->flag = 1
#define __FREE(block) block->flag = 0
#define __CHECK_FREE(block)	!(block->flag & 1)

typedef struct DataNode {
    unsigned char flag;
    unsigned int header;

    char data[0];

    struct DataNode *next;

    struct DataNode *prev;
} DataNode;

typedef struct {
    DataNode *chunckList[__CHUNK_NUM];
} MemList;


static MemList *memTable = NULL;

/*
 * This function, defined in bulk.c, allocates a contiguous memory
 * region of at least size bytes.  It MAY NOT BE USED as the allocator
 * for pool-allocated regions.  Memory allocated using bulk_alloc()
 * must be freed by bulk_free().
 *
 * This function will return NULL on failure.
 */
extern void *bulk_alloc(size_t size);

/*
 * This function is also defined in bulk.c, and it frees an allocation
 * created with bulk_alloc().  Note that the pointer passed to this
 * function MUST have been returned by bulk_alloc(), and the size MUST
 * be the same as the size passed to bulk_alloc() when that memory was
 * allocated.  Any other usage is likely to fail, and may crash your
 * program.
 */
extern void bulk_free(void *ptr, size_t size);

/*
 * This function computes the log base 2 of the allocation block size
 * for a given allocation.  To find the allocation block size from the
 * result of this function, use 1 << block_size(x).
 *
 * Note that its results are NOT meaningful for any
 * size > 4088!
 *
 * You do NOT need to understand how this function works.  If you are
 * curious, see the gcc info page and search for __builtin_clz; it
 * basically counts the number of leading binary zeroes in the value
 * passed as its argument.
 */
static inline __attribute__((unused)) int block_index(size_t x)
{
    if (x <= 8) {
        return 5;
    } else {
        return 32 - __builtin_clz((unsigned int)x + 7);
    }
}

static void free_block_node_data(DataNode *node, unsigned int size)
{
    DataNode **list = &memTable->chunckList[block_index(size - __OFFICE_SIZE) - 5];
    if (*list != NULL)
        (*list)->prev = node;

    node->prev = NULL;
    node->next = *list;
    *list = node;
}

/*
 * You must implement malloc().  Your implementation of malloc() must be
 * the multi-pool allocator described in the project handout.
 */
void *malloc(size_t size)
{
    unsigned int requestMemSize  = 0;
    if (memTable == NULL) {
        memTable = (MemList *)sbrk(__CHUNK_SIZE); // malloc 4096 byte data
        if (memTable == NULL)
            return NULL;
        memset(memTable, 0, sizeof(MemList));
    }
    if (size <= 0)
        return NULL;

    requestMemSize = (size/8 + 1) * 8;
    if (requestMemSize > (__CHUNK_SIZE - __OFFICE_SIZE)) {
        
        DataNode *chunk = bulk_alloc(requestMemSize + __OFFICE_SIZE);
        chunk->header = requestMemSize + __OFFICE_SIZE;
        __ALLOC(chunk);
        return chunk->data;

    } else if (requestMemSize <= (__CHUNK_SIZE - __OFFICE_SIZE)) {

        for (int i = block_index(requestMemSize) - 5; i < __CHUNK_NUM; i++) {
            DataNode **list = &memTable->chunckList[i];
            if (*list != NULL) {
                DataNode *node = *list;
                *list = (*list)->next;
                if (*list != NULL)
                    (*list)->prev = NULL;
                node->next = NULL;
                __ALLOC(node);
                return node->data;
            }
        }

        DataNode *block = (DataNode *)sbrk(__CHUNK_SIZE);
        if (block == NULL)
            return NULL;
        unsigned int allocSize = pow(2, block_index(requestMemSize));
        block->header = allocSize;
        __ALLOC(block);

        int replaceSize = 0;
        replaceSize = __CHUNK_SIZE - allocSize;
        for (int size = 4096; size >= 32; size /= 2) {
            while ( replaceSize >= size) {
                DataNode *newBlock = block + allocSize;
                newBlock->header = size;
                __FREE(newBlock);

                free_block_node_data(newBlock, size);
                replaceSize -= size;
                allocSize += size;
            }
        }
        return block->data;
    }
    return NULL;
}

/*
 * You must also implement calloc().  It should create allocations
 * compatible with those created by malloc().  In particular, any
 * allocations of a total size <= 4088 bytes must be pool allocated,
 * while larger allocations must use the bulk allocator.
 *
 * calloc() (see man 3 calloc) returns a cleared allocation large enough
 * to hold nmemb elements of size size.  It is cleared by setting every
 * byte of the allocation to 0.  You should use the function memset()
 * for this (see man 3 memset).
 */
void *calloc(size_t nmemb, size_t size)
{
    unsigned int resize = ((nmemb * size) / 8 + 1) * 8;
    void *data = malloc(resize);
    if (data != NULL)
        memset(data, 0, resize);

    return data;
}

/*
 * You must also implement realloc().  It should create allocations
 * compatible with those created by malloc(), honoring the pool
 * alocation and bulk allocation rules.  It must move data from the
 * previously-allocated block to the newly-allocated block if it cannot
 * resize the given block directly.  See man 3 realloc for more
 * information on what this means.
 *
 * It is not possible to implement realloc() using bulk_alloc() without
 * additional metadata, so the given code is NOT a working
 * implementation!
 */
void *realloc(void *ptr, size_t size)
{
    unsigned int resize = (size / 8 + 1) * 8;
    if (ptr == NULL)
        return malloc(resize);
    else {
        if (resize == 0) {
            free(ptr);
            return NULL;
        } else {
            DataNode *block = ptr - __OFFICE_SIZE;
            unsigned int blockSize = (block->header & 0xFFFFFFE0);
            if (resize == (blockSize - __OFFICE_SIZE))
                return ptr;
            if (blockSize <= __CHUNK_SIZE) {
                if (block_index(resize) == block_index(blockSize - __OFFICE_SIZE))
                    return ptr;
                else {
                    char data[__CHUNK_SIZE] = { 0 };
                    memcpy(data, ptr, blockSize - __OFFICE_SIZE);
                    free(ptr);
                    void *rePtr = malloc(resize);
                    DataNode *reBlock = rePtr - __OFFICE_SIZE;
                    unsigned int newSize = reBlock->header & 0xFFFFFFE0;
                    if (newSize < blockSize)
                        memcpy(rePtr, data, newSize - __OFFICE_SIZE);
                    else
                        memcpy(rePtr, data, blockSize - __OFFICE_SIZE);
                    return rePtr;
                }

            } else if (blockSize > __CHUNK_SIZE) {
                void *rePtr = malloc(resize);
                DataNode *reBlock = rePtr - __OFFICE_SIZE;
                unsigned int newSize = reBlock->header & 0xFFFFFFE0;
                if (newSize < blockSize)
                    memcpy(rePtr, ptr, newSize - __OFFICE_SIZE);
                else
                    memcpy(rePtr, ptr, blockSize - __OFFICE_SIZE);

                free(ptr);
                return rePtr;
            }
        }
    }
    return NULL;
}

/*
 * You should implement a free() that can successfully free a region of
 * memory allocated by any of the above allocation routines, whether it
 * is a pool- or bulk-allocated region.
 *
 * The given implementation does nothing.
 */
void free(void *ptr)
{
    DataNode *block = ptr - __OFFICE_SIZE;
    if (__CHECK_FREE(block))
        return;
    __FREE(block);

    if (block->header <= __CHUNK_SIZE)
        free_block_node_data(block, block->header);
    else
        bulk_free(block, block->header);
    return;
}


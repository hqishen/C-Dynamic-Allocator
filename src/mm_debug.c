#include <string.h>
#include <stdio.h>
#include <unistd.h>

/* When requesting memory from the OS using sbrk(), request it in
* increments of CHUNK_SIZE. */


#define CHUNK_SIZE (1<<12)
#define SET_BLOCK_ALLOC(list)	(list->header |= 0x00000001)
#define SET_BLOCK_FREE(list)	(list->header &= ~0x00000001)
#define BLOCK_IS_ALLOC(list)	(list->header & 0x00000001)
#define BLOCK_IS_FREE(list)		(!(list->header & 0x00000001))
#define GET_BLOCK_SIZE(list)	(list->header & 0xFFFFFFE0)
#define NBLOCK_SIZE 8
#define HEADER_SIZE sizeof(size_t)

typedef struct freeBlock {
	size_t header;
	char data[0];
    struct freeBlock *next;
    struct freeBlock *prev;
} freeBlock;

typedef struct freeListTable {
	freeBlock *normalBlockList[NBLOCK_SIZE];
} freeListTable;

static freeListTable *Ftable = NULL;
int debugSwitch = 0;

#define Debug(fmt, args...)	do {								\
    if (debugSwitch) {											\
		fprintf(stderr, "LINE %d: "fmt"\n", __LINE__, ##args);	\
    }															\
} while (0)

static double power(double base, int exponent)
{
	double result = 1.0;
	int i = 0;

	for (i = 0; i < exponent; i++)
	{
		result *= base;
	}

	return result;
}

static size_t align8Byte(size_t size)
{
	if ((size & 0x7) == 0) {
		return size;
	}

	return ((size >> 3) + 1) << 3;
}

static freeListTable *malloc_init()
{
	Debug("in malloc init");

	freeListTable *Ftable = (freeListTable *)sbrk(CHUNK_SIZE);
	if (Ftable == (void *)-1) {
		return NULL;
	}

	memset(Ftable, 0, sizeof(freeListTable));

	return Ftable;
}

/* The standard allocator interface from stdlib.h.  These are the
 * functions you must implement, more information on each function is
 * found below. They are declared here in case you want to use one
 * function in the implementation of another. */
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);

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
static inline __attribute__((unused)) int block_index(size_t x) {
    if (x <= 8) {
        return 5;
    } else {
        return 32 - __builtin_clz((unsigned int)x + 7);
    }
}

static void add_node_to_free_list(freeBlock *block, size_t blockSize)
{
	// 在最前面入链
	freeBlock **list = &Ftable->normalBlockList[block_index(blockSize - HEADER_SIZE) - 5];
	if (*list != NULL) {
		(*list)->prev = block;
	}
	block->prev = NULL;
	block->next = *list;
	// 改变此list的头部指向
	*list = block;
	Debug("add to list index %d, size %lu, block %p", block_index(blockSize - HEADER_SIZE) - 5, blockSize, block);
}

static void splitBlock(void *baseAddr, int allocSize, int remainSize)
{
	for (int size = 2048; size >= 32; size /= 2) {
		while (remainSize >= size) {
			freeBlock *newBlock = baseAddr + allocSize;
			newBlock->header = size;

			SET_BLOCK_FREE(newBlock);
			add_node_to_free_list(newBlock, size);

			remainSize -= size;
			allocSize += size;
		}
	}
}

/*
 * You must implement malloc().  Your implementation of malloc() must be
 * the multi-pool allocator described in the project handout.
 */
void *malloc(size_t size)
{
	Debug("---- malloc ----");
	if (Ftable == NULL) {
		// free list table初始化
		Ftable = malloc_init();
		if (Ftable == NULL) {
			return NULL;
		}
	}
	if (size <= 0) {
		return NULL;
	}

	size_t usableSize = align8Byte(size);
	Debug("size = %lu, usableSize = %lu", size, usableSize);
	if (usableSize <= (CHUNK_SIZE - HEADER_SIZE)) {
		// 拿到对应的block
		int index = block_index(usableSize) - 5;
		for (int i = index; i < NBLOCK_SIZE; i++) {
			freeBlock **list = &Ftable->normalBlockList[i];
			if (*list != NULL) {
				freeBlock *returnBlock = *list;
				// 双向摘链
				*list = (*list)->next;
				if (*list != NULL) {
					(*list)->prev = NULL;
				}
				returnBlock->next = NULL;
				// 设置已分配标记
				SET_BLOCK_ALLOC(returnBlock);
				// 返回用户可使用区域
				Debug("block found in %d, block %p, return %p", i, returnBlock, returnBlock->data);
				return returnBlock->data;
			}
		}
		// 无可用block，申请新的空间
		freeBlock *block = (freeBlock *)sbrk(CHUNK_SIZE);
		if (block == (void *)-1) {
			return NULL;
		}
		// 但是实际分配的大小为2的m次方，m >= 5 && m <= 12  
		size_t allocSize = power(2, block_index(usableSize));
		// 头部大小就是这个block的实际大小
		block->header = allocSize;
		Debug("small: block not found, alloc size %lu, block %p, return %p", allocSize, block, block->data);
		// 设置已分配标记
		SET_BLOCK_ALLOC(block);
		// 分割剩余block大小，存储至free list table
		splitBlock(block, allocSize, CHUNK_SIZE - allocSize);
		return block->data;
	} else {
		// 申请large block
		freeBlock *largeBlock = bulk_alloc(usableSize + HEADER_SIZE);
		largeBlock->header = usableSize + HEADER_SIZE;
		SET_BLOCK_ALLOC(largeBlock);
		Debug("large: alloc size %lu, block %p, return %p", usableSize + HEADER_SIZE, largeBlock, largeBlock->data);
		return largeBlock->data;
	}
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
	Debug("---- calloc ----");
	size_t usableSize = align8Byte(nmemb * size);
    void *ptr = malloc(usableSize);
	if (ptr != NULL) {
		memset(ptr, 0, usableSize);
	}
	Debug("calloc clear size %lu, return %p", usableSize, ptr);

    return ptr;
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
	Debug("---- realloc ----");
	size_t usableSize = align8Byte(size);
	if (ptr == NULL) {
		return malloc(usableSize);
	} else {
		if (usableSize == 0) {
			free(ptr);
			return NULL;
		} else {
			freeBlock *block = ptr - HEADER_SIZE;
			size_t blockSize = GET_BLOCK_SIZE(block);
			Debug("realloc size %lu, usableSize %lu, blockSize %lu", size, usableSize, blockSize);
			if (usableSize == (blockSize - HEADER_SIZE)) {
				// 一样大的块，无需分配，直接返回
				Debug("same size! return %p", ptr);
				return ptr;
			}
			if (blockSize <= CHUNK_SIZE) {
				// 原block为小块
				if (block_index(usableSize) == block_index(blockSize - HEADER_SIZE)) {
					// 同幂的list，直接返回，无需分配
					Debug("same power! return %p", ptr);
					return ptr;
				} else {
					// 不同幂的list，重新分配
					char backData[CHUNK_SIZE] = { 0 };
					// 备份释放之前的数据
					memcpy(backData, ptr, blockSize - HEADER_SIZE);
					// 释放小块
					free(ptr);
					// 申请新的空间，申请到的有可能是刚刚上链的空间
					void *newPtr = malloc(usableSize);
					freeBlock *newBlock = newPtr - HEADER_SIZE;
					size_t newBlockSize = GET_BLOCK_SIZE(newBlock);
					// 数据拷贝，看现空间大小有不同情况
					if (newBlockSize < blockSize) {
						memcpy(newPtr, backData, newBlockSize - HEADER_SIZE);
					} else {
						memcpy(newPtr, backData, blockSize - HEADER_SIZE);
					}
					return newPtr;
				}
			} else {
				// 原block为大块
				void *newPtr = malloc(usableSize);
				freeBlock *newBlock = newPtr - HEADER_SIZE;
				size_t newBlockSize = GET_BLOCK_SIZE(newBlock);
				// 数据拷贝，看现空间大小有不同情况
				if (newBlockSize < blockSize) {
					memcpy(newPtr, ptr, newBlockSize - HEADER_SIZE);
				} else {
					memcpy(newPtr, ptr, blockSize - HEADER_SIZE);
				}
				// 释放大块
				free(ptr);
				return newPtr;
			}
		}
	}
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
	Debug("---- free ----");
	if (ptr == NULL) {
		return;
	}

	freeBlock *block = ptr - HEADER_SIZE;
	if (BLOCK_IS_FREE(block)) {
		return;
	}
	
	SET_BLOCK_FREE(block);
	if (block->header <= CHUNK_SIZE) {
		Debug("free small ptr %p, size %lu", ptr, block->header);
		add_node_to_free_list(block, block->header);
	} else {
		Debug("free large ptr %p, size %lu", ptr, block->header);
		bulk_free(block, block->header);
	}

    return;
}


int main(void) {
	return 0;
}

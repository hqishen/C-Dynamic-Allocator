#include <string.h>
#include <stdio.h>
#include <unistd.h>

/* When requesting memory from the OS using sbrk(), request it in
* increments of CHUNK_SIZE. */
#define CHUNK_SIZE (1<<12)

typedef struct MemNode {
	size_t header;
	char data[0];
    struct MemNode *next;
    struct MemNode *prev;
} MemNode;

typedef struct MemList {
	MemNode *chunkList[8];
} MemList;

int printFlag = 0;
#define my_print(fmt, args...) if (printFlag) fprintf(stderr, "File Name:%s, Func Name:%s, Line:%d " fmt,__FILE__, __FUNCTION__, __LINE__, ##args);
static MemList *gMemDataTable = NULL;

/* The standard allocator interface from stdlib.h.  These are the
 * functions you must implement, more information on each function is
 * found below. They are declared here in case you want to use one
 * function in the implementation of another. */
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void set_chunk_alloc_flag(MemNode *node);
void set_chunk_free_flag(MemNode *node);
int get_chunk_free_flag(MemNode *node);
int get_chunk_size(MemNode *node);
double _pow(double b, int e);
size_t alignment(size_t size);

void set_chunk_alloc_flag(MemNode *node)
{
	node->header = node->header |0x00000001;
}

void set_chunk_free_flag(MemNode *node)
{
	node->header &= ~0x00000001;
}


int get_chunk_free_flag(MemNode *node) 
{
	return (!(node->header & 0x00000001));
}

int get_chunk_size(MemNode *node)
{
	return (node->header & 0xFFFFFFE0);
}

double _pow(double b, int e)
{
	double result = 1;
	for (int i = 0; i < e; i++)
		result = result * b;

	return result;
}

size_t alignment(size_t size)
{
	if ((size & 0x7) == 0)
		return size;
	return ((size >> 3) + 1) << 3;
}



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

static void create_node_data(MemNode *chunk, size_t size)
{
	// alloc list
	MemNode **list = &gMemDataTable->chunkList[block_index(size - sizeof(size_t)) - 5];
	if (*list != NULL) {
		(*list)->prev = chunk;
	}
	chunk->prev = NULL;
	chunk->next = *list;
	// create node
	*list = chunk;
	my_print("current index %d, mem size %lu, mem chunk: %p \n", block_index(size - sizeof(size_t)) - 5, size, chunk);
}

static void split_mm(void *bAddr, int alloc_size, int total_size)
{
	for (int size = 2048; size >= 32; size /= 2) 
	{
		while (total_size >= size) 
		{
			MemNode *node = bAddr + alloc_size;
			node->header = size;
			set_chunk_free_flag(node);
			create_node_data(node, size);

			total_size -= size;
			alloc_size += size;
		}
	}
}

/*
 * You must implement malloc().  Your implementation of malloc() must be
 * the multi-pool allocator described in the project handout.
 */
 
void *malloc(size_t size)
{
	if (gMemDataTable == NULL) {
		// create a piece of memory 
		gMemDataTable = (MemList *)sbrk(CHUNK_SIZE);
		if (gMemDataTable == NULL) {
			return NULL;
		}
		// reset this memory
		memset(gMemDataTable, 0, sizeof(MemList));
	}
	if (size <= 0) {
		return NULL;
	}
	
	// alignment  size of memroy
	size_t get_size = alignment(size);
	my_print("alignment size = %lu, get_size = %lu \n", size, get_size);
	
	if (get_size <= (CHUNK_SIZE - sizeof(size_t)))
	{
		int index = block_index(get_size) - 5;
		// Traversing the list
		for (int i = index; i < 8; i++)
		{
			MemNode **list = &gMemDataTable->chunkList[i];
			if (*list != NULL) 
			{
				MemNode *ptr = *list;
				*list = (*list)->next;
				if (*list != NULL)
				{
					(*list)->prev = NULL;
				}
				// get useless block
				ptr->next = NULL;
				my_print("Get block in %d, block %p, return %p \n", i, ptr, ptr->data);
				set_chunk_alloc_flag(ptr);
				return ptr->data;
			}
		}
		MemNode *block = (MemNode *)sbrk(CHUNK_SIZE);
		if (block == NULL) {
			return NULL;
		}
		// get alloc size
		size_t alloc_size = _pow(2, block_index(get_size));
		// put the size to the node header
		block->header = alloc_size;
		my_print("alloc size %lu, block %p, return %p \n", alloc_size, block, block->data);
		
		// setup this mem flag
		set_chunk_alloc_flag(block);
		split_mm(block, alloc_size, CHUNK_SIZE - alloc_size);
		
		return block->data;
	} else {
		MemNode *newptr = bulk_alloc(get_size + sizeof(size_t));
		newptr->header = get_size + sizeof(size_t);
		set_chunk_alloc_flag(newptr);
		my_print("alloc mem size %lu, block addr %p, data %p", get_size + sizeof(size_t), newptr, newptr->data);
		return newptr->data;
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
	
	size_t get_size = alignment(nmemb * size);
    void *ptr = malloc(get_size);
	if (ptr != NULL)
		memset(ptr, 0, get_size);
	my_print("clear mem size %lu, return %p \n", get_size, ptr);

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
	size_t get_size = alignment(size);
	if (ptr == NULL) 
	{
		return malloc(get_size);
	} 
	else
	{
		if (get_size == 0) 
		{
			free(ptr);
			return NULL;
		}
		else 
		{
			MemNode *block = ptr - sizeof(size_t);
			size_t block_size = get_chunk_size(block);
			my_print(" realloc mem size %lu, get_size %lu, block_size %lu \n", size, get_size, block_size);
			if (get_size == (block_size - sizeof(size_t))) 
			{				
				// get the same size,return ptr
				return ptr;
			}
			
			if (CHUNK_SIZE >= block_size)
			{
				if (block_index(get_size) == block_index(block_size - sizeof(size_t))) 
				{
					my_print("get the same power and will return %p \n", ptr);
					return ptr;
				} 
				else 
				{
					
					char mem_data[CHUNK_SIZE] = {0};
					// backup this mem data
					memcpy(mem_data, ptr, block_size - sizeof(size_t));
					// free this ptr
					free(ptr);
					
					void *newPtr = malloc(get_size);
					MemNode *newBlock = newPtr - sizeof(size_t);
					size_t newBlockSize = get_chunk_size(newBlock);
					// new block size and put backup data to the mem
					if (newBlockSize < block_size) 
					{
						memcpy(newPtr, mem_data, newBlockSize - sizeof(size_t));
					} 
					else 
					{
						memcpy(newPtr, mem_data, block_size - sizeof(size_t));
					}
					return newPtr;
				}
			}
			else 
			{
				// backup this mem data
				void *newPtr = malloc(get_size);
				MemNode *newBlock = newPtr - sizeof(size_t);
				size_t newBlockSize = get_chunk_size(newBlock);
				//new block size and put backup data to the mem
				if (newBlockSize < block_size) 
				{
					memcpy(newPtr, ptr, newBlockSize - sizeof(size_t));
				} 
				else 
				{
					memcpy(newPtr, ptr, block_size - sizeof(size_t));
				}
				// free this ptr
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
	if (ptr == NULL) {
		return;
	}

	MemNode *block = ptr - sizeof(size_t);
	if (get_chunk_free_flag(block))
		return;
	
	
	set_chunk_free_flag(block);
	if (block->header <= CHUNK_SIZE) {
		my_print("free mem: %p and size %lu \n", ptr, block->header);
		create_node_data(block, block->header);
	} else {
		my_print("free mem: %p and size %lu \n", ptr, block->header);
		bulk_free(block, block->header);
	}
	
    return;
}

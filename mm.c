/*
 * mm.c - Explicit list implementation of malloc and free
 * Doubly-linked list with headers and footers.
 * Uses first-fit
 * Coalesces on all four cases.
 *
 * Preprocessor macros and helper functions taken from the textbook.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
	/* Team name */
	"scatman",
	/* First member's full name */
	"Marcin Swieczkowski",
	/* First member's email address */
	"scatman@bu.edu",
	/* Second member's full name (leave blank if none) */
	"",
	/* Second member's email address (leave blank if none) */
	""
};


/* Basic constants and macros */
#define ALIGN(p) (((size_t)(p) + (7)) & ~(0x7))
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))
#define SIZE_PTR(p)  ((size_t*)(((char*)(p)) - SIZE_T_SIZE))

#define WSIZE 4       /* Word and header/footer size (bytes) */ 
#define DSIZE 8       /* Doubleword size (bytes) */

#define MAX(x, y) ((x) > (y) ? (x) : (y))  

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc)) 

/* Read and write a word at address p */
#define GET(p) (*(unsigned int *)(p))           
#define PUT(p, val) (*(unsigned int *)(p) = (val)) 

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7)                  
#define GET_ALLOC(p) (GET(p) & 0x1)             

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)))

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(HDRP((char *)(bp))) + 2*WSIZE)
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE((char *)(bp)-DSIZE) - 2*WSIZE)

/* Given free block ptr bp, compute address of next and previous free blocks */
#define GET_NEXT_FREE(bp) (*(void **)((char *)(bp) + DSIZE))
#define GET_PREV_FREE(bp) (*(void **)(char *)(bp))
#define SET_NEXT_FREE(bp, ptr) (GET_NEXT_FREE(bp) = ptr)
#define SET_PREV_FREE(bp, ptr) (GET_PREV_FREE(bp) = ptr)


/* pointers to free list */
void * free_list;

/* Function prototypes for internal helper routines*/
static void *place(void *bp, size_t asize);
static void *find_fit(size_t asize);
static void coalesce(void *ptr);
static void add(void *ptr);
static void delete(void *ptr);
int mm_check();

/*
 * mm_init - Allocate a padding of 4 bytes for alignment
 */
int mm_init(void) {
	void *heap_bottom = mem_heap_lo();
	int i;
	
	/* Initialize storage */
	free_list = NULL;

	if ((heap_bottom = mem_sbrk(2*WSIZE)) == (void *)-1) {
		return -1;
	}
	
	/* Alignment */
	PUT(heap_bottom, PACK(0,1));
	PUT((char *)heap_bottom + WSIZE, PACK(0,1));
	
	return 0;
}

/* 
 * mm_malloc - searches the free list for a first fit free block
 * If no free blocks are found, mm_malloc will request more memory
 */
void *mm_malloc(size_t size) {	
	if (size <= 0) {
		return NULL;
	}
	unsigned int asize;
	void *list;
	
	/* Adjust size */
	if (size <= 4*DSIZE) {
		asize = 4*DSIZE;
	} 
	else {
		asize = ALIGN(size);
	}
	
	/* search free list for first free block of size asize */
	if ((list = find_fit(asize)) != NULL) {
		return place(list, asize);
	}
	
	/* If no block can be found, request more memory in heap */
	list = mem_sbrk(asize + 2*WSIZE);
	if ((long)list == -1) {
		return NULL;
	}
	/* otherwise, allocate memory and set epilogue */
	PUT(HDRP(list), PACK(asize, 1));
    	PUT(FTRP(list), PACK(asize, 1));
    	PUT(FTRP(list) + WSIZE, PACK(0, 1));
	return list;
}

/*
 * place - sets a portion of a free block to be allocated
 * It will either use the entire free block, or split it and only use a portion.
 * If it splits, the allocated portion will be placed at the end of the block.
 */
static void *place(void *bp, size_t asize) {
	int bsize = GET_SIZE(HDRP(bp));
	if (bsize >= asize + 32) {
		int csize = bsize - asize - 2*WSIZE;

		PUT(HDRP(bp), PACK(csize, 0));
		PUT(FTRP(bp), PACK(csize, 0));
		void *p = NEXT_BLKP(bp);
		PUT(HDRP(p), PACK(asize, 1));
		PUT(FTRP(p), PACK(asize, 1));
		return p;	
	}			
	delete(bp);
	PUT(HDRP(bp), PACK(bsize, 1));
	PUT(FTRP(bp), PACK(bsize, 1));
	return bp;
}

/*
 * find_fit - first-fit search for free block of size asize
 * If no blocks are found in 500 iterations, we assume there aren't any - this speeds up the program greatly
 */
static void *find_fit(size_t asize) {
	void *bp;
	int i;
	
	for (bp = free_list, i = 0; bp != NULL && i < 500; bp = GET_NEXT_FREE(bp), i ++) {
		if (GET_SIZE(HDRP(bp)) > asize)
			return bp;
	}
	
	return NULL; /* no fit */
}

/*
 * mm_free - Freeing a block.
 * Adds to free list, or coalesces if there are adjacent free blocks.
 * Tries to coalesce if there are blocks in the free list.
 */
void mm_free(void *bp){
	if (bp == 0) {
		return;
	}
	size_t size = GET_SIZE(HDRP(bp));
	
	PUT(HDRP(bp), PACK(size, 0));
	PUT(FTRP(bp), PACK(size, 0));

	if (free_list != NULL) {
		coalesce(bp);
	} else {
		add(bp);
	}
}

/*	
 * coalesce - coalesces free blocks to reduce fragmentation
 
 * cases:
 * 1. next and prev allocated
 * 2. prev allocated, next free
 * 3. prev free, next allocated
 * 4. next and prev free
 *
 * The function will merge the free block with any adjacent
 * free blocks and return a pointer to a single free block.
 */
static void coalesce(void *ptr) {
	size_t next_alloc = GET_ALLOC((char *)(FTRP(ptr)) + WSIZE);
	size_t prev_alloc = GET_ALLOC((char *)(ptr) - DSIZE);
	size_t size = GET_SIZE(HDRP(ptr));

	if (prev_alloc && next_alloc) {   // case 1
		add(ptr);
	} 
	else if (prev_alloc && !next_alloc) {  	// case 2
		size += GET_SIZE(HDRP(NEXT_BLKP(ptr))) + 2*WSIZE;
		delete(NEXT_BLKP(ptr));
		PUT(HDRP(ptr), PACK(size, 0));
		PUT(FTRP(ptr), PACK(size, 0));
		add(ptr);
	} 
	else if (!prev_alloc && next_alloc) {	// case 3
		ptr = PREV_BLKP(ptr);
		size += GET_SIZE(HDRP(ptr)) + 2*WSIZE;
		PUT(HDRP(ptr), PACK(size, 0));
		PUT(FTRP(ptr), PACK(size, 0));
	} 
	else { 	// case 4
		void * prev = PREV_BLKP(ptr);
		void * next = NEXT_BLKP(ptr);		
		size += GET_SIZE(HDRP(prev)) + GET_SIZE(HDRP(next)) + 4*WSIZE;
		PUT(HDRP(prev), PACK(size, 0));
		PUT(FTRP(prev), PACK(size, 0));
		delete(next);
	}
}

/*
 * add - add block bp to the free list, making it the first element in the list.
 */
static void add(void *bp) {
	void *head = free_list;
	SET_NEXT_FREE(bp, head);
	SET_PREV_FREE(bp, NULL);
	if (head != NULL)
		SET_PREV_FREE(head, bp);
	free_list = bp;
}

/* 
 * delete - delete block from the list pointed to by bp
 */
static void delete(void *bp) {
	void *next = GET_NEXT_FREE(bp);
	void *prev = GET_PREV_FREE(bp);
	
	if (prev == NULL) {
		free_list = next;
		if (next != NULL) {
			SET_PREV_FREE(next, NULL);
		}
	} 
	else {
		SET_NEXT_FREE(prev, next);
		if (next != NULL) {
			SET_PREV_FREE(next, prev);
		}
	}
}

/*
 * mm_check - 
 * Checks the free list for:
 * 1. whether every block in the free list is marked as free
 * 2. whether every pointer is in the heap
 * 3. whether every block is aligned
 * 4. whether contiguous blocks are allocated (coalesced properly)
 */
int mm_check() {
	void *list = free_list;
	int consistent = 1;
	printf("Checking for errors.\n");	
	
	while (list != NULL) {
			if (GET_ALLOC(list) != 0) {
				printf("ERROR: The free block is not marked as free.\n");
				consistent = 0;
			}
	
			if (!(list <= mem_heap_hi() && list >= mem_heap_lo())) {
				printf("ERROR: The pointer is outside the heap.\n");
				consistent = 0;
			}
			
			if ((size_t)ALIGN((long)list) != (size_t)list) {
				printf("ERROR: The block is not aligned\n");
				consistent = 0;
			}
			
			if (GET_ALLOC(NEXT_BLKP(list)) != 1 || GET_ALLOC(PREV_BLKP(list)) != 1) {
				printf("ERROR: Two or more contiguous blocks are free.\n");
			}
			
			list = GET_NEXT_FREE(list);
		}
		printf("Error checking complete.\n");
		
		return consistent;
}

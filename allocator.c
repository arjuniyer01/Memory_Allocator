
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include "allocator.h"
 
/*
 * This structure serves as the header for each allocated and free block.
 * It also serves as the footer for each free block but only containing size.
 */
typedef struct header {           

    int block_info;
    /*
     * Size of the block is always a multiple of 8.
     * Size is stored in all block headers and in free block footers.
     *
     * curr_s is stored only in headers using the two least significant bits.
     *   Bit0 => least significant bit, last bit
     *   Bit0 == 0 => free block
     *   Bit0 == 1 => allocated block
     *
     *   Bit1 => second last bit 
     *   Bit1 == 0 => previous block is free
     *   Bit1 == 1 => previous block is allocated
     * 
     * End Mark: 
     *  The end of the available memory is indicated using a block_info of 1.
     * 
     * Examples:
     * 
     * 1. Allocated block of size 24 bytes:
     *    Allocated Block Header:
     *      If the previous block is free      p-bit=0 block_info would be 25
     *      If the previous block is allocated p-bit=1 block_info would be 27
     * 
     * 2. Free block of size 24 bytes:
     *    Free Block Header:
     *      If the previous block is free      p-bit=0 block_info would be 24
     *      If the previous block is allocated p-bit=1 block_info would be 26
     *    Free Block Footer:
     *      block_info should be 24
     */
} header;         

// the block at the lowest address.
header *first_block = NULL;     

/* Size of heap allocation padded to round to nearest page size.
 */
int totalallocation;

 
/* 
 * Function for allocating 'size' bytes of heap memory.
 * Argument size: requested size for the payload
 * Returns address of allocated block (payload) on success.
 * Returns NULL on failure.
 *
 * This function:
 * - Checks size - Returns NULL if not positive or if larger than heap space.
 * - Determines block size rounding up to a multiple of 8 
 *   and possibly adding padding as a result.
 *
 * - Uses BEST-FIT PLACEMENT POLICY to chose a free block
 *
 * - If the BEST-FIT block that is found is exact size match
 *   - 1. Update all heap blocks as needed for any affected blocks
 *   - 2. Returns the address of the allocated block payload
 *
 * - If the BEST-FIT block that is found is large enough to split 
 *   - 1. SPLITS the free block into two valid heap blocks:
 *         1. an allocated block
 *         2. a free block
 *       - Updates all heap block header(s) and footer(s) 
 *              as needed for any affected blocks.
 *   - 2. Returns the address of the allocated block payload
 *
 * - If a BEST-FIT block found is NOT found, returns NULL
 *   Returns NULL unable to find and allocate block for desired size
 *
 *
 */
void* alloc_bf(int size) {

    if(size <= 0)
        return NULL;

    if(size > totalallocation-4)
        return NULL;

    // Initialising this that points to location of the heap start
    header *this = first_block;
    // Initialising best as NULL. Intended to hold the best fit block address
    header *best = NULL;

    // Initialises padding accordingly
    int padding = ((size+4)%8 == 0) ? 0 :(8 - ((size + 4) %8));
    int total_size = size + 4 + padding;

    while (this->block_info != 1){

        // Checking if the block is empty and has a size 
        // large enough to fit our allocation
         if (this->block_info%2 == 0 && (this->block_info/8)*8 >= (size + 4)){
             // If we don't have a best yet, it is assigned the 
             // first free block large enough
             if(best == NULL){
                 best = this;
             }else{ // else it is assigned a new block if it is more efficient
                 if ((this->block_info/8)*8 < (best->block_info/8)*8)
                 best = this;
             }
         }

         // If we have found the perfect assignment already the loop breaks
         if(best != NULL){
             if ((best->block_info/8)*8 == total_size)
             break;
         }

         // Moving the this pointer to the next block before following iteration
         this = (header*) ((char*)this + (this->block_info/8)*8);

    }

    if (best != NULL) {

        // Fragmentation
        if (((best->block_info / 8) * 8 - total_size) >= 8) {

            // Initialising split(header) that points to location where block needs to be split
            header *split =(header*)((char*)best + total_size);
            // Initialising the block_info of the split block with the leftover size
            // and incrementing by 2 to signify only active p-bit
            split->block_info = (best->block_info / 8) * 8 - total_size + 2;

            // Initialising a footer that points to location where a footer
            // needs to be added for the new empty block
            header *footer =(header*)((char*)split + (split->block_info/8)*8 - 4);
            // Initialising the block_info of the footer with the size of the split block
            footer->block_info = split -> block_info-2; 

        } else{ 

            // Initialising nextblock that points to location of the next block's header
            header *nextblock = (header*) ((char*)best + (best->block_info/8)*8);
            // Changes the block_info of the next block only if it is not the end of the heap
            if(nextblock->block_info !=1){
                nextblock->block_info += 2; 
            }
        }

        // Changes the block_info of the best fit block.
        // Increments it by 1 to signify allocation i.e. active a-bit
        best->block_info = 1 + (best->block_info)%8 + total_size;
        
        // Returns pointer to the best fit block
        return ((char*)best+4);
    }

    // Returns NULL if best fit was not found
    return NULL;
} 
 
/* 
 * Function for freeing up a previously allocated block.
 * Argument ptr: address of the block to be freed up.
 * Returns 0 on success.
 * Returns -1 on failure.
 * 
 * - Returns -1 if ptr is NULL.
 * - Returns -1 if ptr is not a multiple of 8.
 * - Returns -1 if ptr is outside of the heap space.
 * - Returns -1 if ptr block is already freed.
 * - Update header(s) and footer as needed.
 */                    
int free_block(void *ptr) {

    if(ptr == NULL)
    return -1;

    if (((unsigned long int)ptr)%8 != 0)
    return -1;

    if ((unsigned long int)ptr < (unsigned long int)first_block || 
    (unsigned long int)ptr > (unsigned long int)first_block + totalallocation)
    return -1;

    // Checking is block has already been freed
    if (((header*)((char*)ptr-4)) -> block_info %2 != 1)
    return -1;

    // Initialising this so it points to the header of the block to free
    header *this = (header*)((char*) ptr - 4);
    (this -> block_info) -= 1; // Decresing block_info by 1 signifying empty block
    int this_size = (this->block_info/8)*8; // storing size of the block

    // Initialising this_foot that points to location where footer should be added
    header *this_foot = (header*) ((char*) this + this_size - 4);
    this_foot->block_info = this_size; // Adding footer

    // Updating p-bit of next block
    header *next = (header*)((char*)this + (this->block_info/8)*8);
    // Changes block_info only if it is not the end of the heap
    if (next->block_info != 1){
        next->block_info -= 2; 
    }

    // Reaches this point and returns 0 only on success
    return 0;
} 

/*
 * Function for traversing heap block list and coalescing all adjacent 
 * free blocks. Used for delayed coalescing.
 */
int coalesce() {

    header *this = first_block;
    header *new_foot;
    int new_size = 0;

    while(this->block_info != 1){
        
        // Check if the block is free
        if (this->block_info%2 == 0)
        {
            // Next block coalescing
            header *next = (header*) ((char*)this + (this->block_info/8)*8);
            if (next->block_info != 1 && next->block_info%2 == 0)
            {
                // Coalescing by adding next block size to block_info of this
                this->block_info += (next->block_info/8)*8;
                // Storing the new size
                new_size = (this->block_info/8)*8;
                new_foot = ((header*)((char*)this + new_size-4));
                new_foot->block_info = new_size;
            }

            // Previous block coalescing
            if((this->block_info)%8 == 0){
                // Getting size of previous from footer
                int prev_size = ((header*)((char*)this - 4))->block_info;
                // Creating prev that points to header of previous block
                header *prev = (header*)((char*)this - prev_size);
                // Coalescing by adding next block size to block_info of prev
                prev->block_info += (this->block_info/8)*8;
                // Storing the new size
                new_size = (prev->block_info/8)*8;
                new_foot = ((header*)((char*)prev + new_size - 4));
                new_foot->block_info = new_size;
            }
            
        }
        // Moving the this pointer to the next block before following iteration
        this = (header*) ((char*)this + (this->block_info/8)*8);
    }

	return 0;
}

 
/* 
 * Function used to initialize the memory allocator.
 * Intended to be called ONLY once by a program.
 * regionSize: the size of the heap space to be allocated.
 * Returns 0 on success.
 * Returns -1 on failure.
 */                    
int initRegion(int regionSize) {    
 
    static int done = 0; 
 
    int pagesize;  
    int mem_padding;   
    void* mem_ptr;
    int dir;

    header* end;
  
    if (0 != done) {
        fprintf(stderr, 
        "Error:mem.c: InitHeap has allocated space during a previous call\n");
        return -1;
    }

    if (regionSize <= 0) {
        fprintf(stderr, "Error:mem.c: Requested block size is not positive\n");
        return -1;
    }

    pagesize = getpagesize();

    // Padding required to round up regionSize 
    mem_padding = regionSize % pagesize;
    mem_padding = (pagesize - mem_padding) % pagesize;

    pagesize = regionSize + mem_padding;

    // allocate memory
    dir = open("/dev/zero", O_RDWR);
    if (-1 == dir) {
        fprintf(stderr, "Error:mem.c: Cannot open /dev/zero\n");
        return -1;
    }
    mem_ptr = mmap(NULL, pagesize, PROT_READ | PROT_WRITE, MAP_PRIVATE, dir, 0);
    if (MAP_FAILED == mem_ptr) {
        fprintf(stderr, "Error:mem.c: mmap cannot allocate space\n");
        done = 0;
        return -1;
    }
  
    done = 1;

    // for double word alignment and end mark
    pagesize -= 8;

    // Set first block
    first_block = (header*) mem_ptr + 1;
    first_block->block_info = pagesize;// Set size    
    first_block->block_info += 2; // Set p-bit

    // Set end mark
    end = (header*)((void*)first_block + pagesize);
    end->block_info = 1;

    // Set footer
    header *footer = (header*) ((void*)first_block + pagesize - 4);
    footer->block_info = pagesize;
  
    return 0;
} 
                  
/* 
 * Function to be used to help visualize the heap structure.
 */                     
void display() {     
 
    int count;
    char curr_s[6];
    char prev_s[6];
    char *begin_a = NULL;
    char *end_a   = NULL;
    int t_size;

    header *this = first_block;
    count = 1;

    int used = 0;
    int free_size = 0;
    int is_used   = -1;

    fprintf(stdout, 
	"--------------------------------- Memory Block ----------------------------------\n");
    fprintf(stdout, "No.\tCurrent\tPrevious\tbegin_address\t\tend_address\t\tSize\n");
    fprintf(stdout, 
	"---------------------------------------------------------------------------------\n");
  
    while (this->block_info != 1) {
        begin_a = (char*)this;
        t_size = this->block_info;
    
        if (t_size % 2 == 0) {
            // t_size even for empty block
            strcpy(curr_s, "FREE ");
            is_used = 0;
        } else {
            strcpy(curr_s, "ALLOC");
            is_used = 1;
            t_size = t_size - 1;
        }

        if (t_size & 2) {
            strcpy(prev_s, "ALLOC");
            t_size = t_size - 2;
        } else {
            strcpy(prev_s, "FREE ");
        }

        if (is_used) 
            used += t_size;
        else 
            free_size += t_size;

        end_a = begin_a + t_size - 1;
    
        fprintf(stdout, "%d\t%s\t%s\t0x%08lx\t0x%08lx\t%4i\n", count
, curr_s, 
        prev_s, (unsigned long int)begin_a, (unsigned long int)end_a, t_size);
    
        this = (header*)((char*)this + t_size);
        count
 = count
 + 1;
    }

    fprintf(stdout, 
	"---------------------------------------------------------------------------------\n");
    fprintf(stdout, 
	"---------------------------------------------------------------------------------\n");
    fprintf(stdout, "Used size = %4d\n", used);
    fprintf(stdout, "Free size = %4d\n", free_size);
    fprintf(stdout, "Total size      = %4d\n", used + free_size);
    fprintf(stdout, 
	"---------------------------------------------------------------------------------\n");
    fflush(stdout);

    return;  
} 
                                     
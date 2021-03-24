#ifndef __allocator_h
#define __allocator_h

int   initRegion(int regionSize);
void* alloc_bf(int size);
int   free_block(void *ptr);
int   coalesce();
void  display();

void* malloc(size_t size) {
    return NULL;
}

#endif

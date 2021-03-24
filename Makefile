myHeap: allocator.c allocator.h
	gcc -g -c -Wall -m32 -fpic allocator.c
	gcc -shared -Wall -m32 -o libheap.so allocator.o

clean:
	rm -rf allocator.o libheap.so

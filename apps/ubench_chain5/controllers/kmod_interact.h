#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h> /* uintmax_t */
#include <sys/mman.h>
#include <unistd.h> /* sysconf */

#define BUF_SIZE 6
#define BUF_SIZE2 6
#define ALLOC_SIZE 2048

//#include <lkmc/pagemap.h> /* lkmc_pagemap_virt_to_phys_user */
//int shmem_fd;

void init_mmap(void* address) {
    int shmem_fd;
    shmem_fd = open("/proc/lkmc_mmap", O_RDWR | O_SYNC);
    address = mmap(NULL, ALLOC_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shmem_fd, 0);
    if (address == MAP_FAILED) {
        perror("mmap failed to get correct address");
        //assert(0); //Removed for testing purposes only
    }
    return;
}

static inline void read_mmap(uint32_t* shmem, uint32_t* user_array) {
    for(int i=0; i<BUF_SIZE; i++) {
	user_array[i] = *((uint32_t*)shmem+i);
    }
}

static inline void read_mmap2(uint32_t* shmem, uint32_t* user_array) {
    for(int i=0; i<BUF_SIZE2; i++) {
	user_array[i] = *((uint32_t*)shmem+i);
    }
}

void clean_mmap(void* shmem) {
    if(munmap(shmem, ALLOC_SIZE)) {
        perror("munmap failed");
        assert(0);
    }
    //close(shmem_fd);
}

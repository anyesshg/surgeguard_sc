#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#define MSR_IA32_PERF_CTL 0x0.00.995		// Copied from x86/include/asm/msr-index.h
#define MSR_IA32_PERF_STATUS 0x0.00598		// Copied from x86/include/asm/msr-index.h

static inline void write_core_msr(int fd, uint64_t freq_hz)
{
    uint64_t msr_value = ((freq_hz/ 100000ULL) << 8);
    if (pwrite(fd, &msr_value, sizeof(msr_value), MSR_IA32_PERF_CTL) != sizeof(msr_value)) {
        perror("pwrite");
        close(fd);
        exit(EXIT_FAILURE);
    }
}


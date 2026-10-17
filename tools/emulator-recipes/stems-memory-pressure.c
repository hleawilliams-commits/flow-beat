#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

/* Emulator-only, bounded fault injection. Exit automatically after 90 seconds.
 * Reserve touched memory only if at least 350 MiB will remain available. */
int main(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return 1;
    char line[256]; long kb = 0;
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "MemAvailable: %ld kB", &kb) == 1) break;
    fclose(f);
    size_t size = 900UL * 1024 * 1024;
    if (kb < (900 + 350) * 1024) {
        fprintf(stderr, "Refusing: available %ld KiB, requires 1250 MiB\n", kb);
        return 2;
    }
    volatile unsigned char *p = mmap(NULL, size, PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return 3;
    for (size_t i = 0; i < size; i += 4096) p[i] = 1;
    printf("READY pid=%d reserved=900 MiB initial_available=%ld KiB\n", getpid(), kb);
    fflush(stdout);
    sleep(90);
    munmap((void *)p, size);
    return 0;
}

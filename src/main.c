#include <stdio.h>

#define USE_MEMORY
#define DEBUG

#include "mem.h"

int main(int argc, char *argv[]) 
{
    MEM_START;

    for (int i = 0; i < 1024; i++) {
        int *s = MEM_MALLOC(i * sizeof(int));
        printf("-->%d\n", i);
        IDLE_MEM_FREE(s);
    }

    MEM_MALLOC(10);

    PRINT_MEM_INFO;
    PRINT_LEAK_INFO;
    MEM_END;

    getchar();
    return 0;
}

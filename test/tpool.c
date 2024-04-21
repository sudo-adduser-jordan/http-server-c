#include <stdio.h>
#include <pthread.h>
#include <stdint.h>

#include "../app/tpool.h"

void task(void *arg)
{
    printf("Thread #%lu working on %p\n", pthread_self(), arg);
}

int main()
{

    puts("Making threadpool with 4 threads");
    threadpool thpool = tpool_init(4);

    puts("Adding 40 tasks to threadpool");
    int i;
    for (i = 0; i < 1000000; i++)
    {
        tpool_add_work(thpool, task, (void *)(uintptr_t)i);
    };

    tpool_wait(thpool);
    puts("Killing threadpool");
    tpool_destroy(thpool);

    return 0;
}
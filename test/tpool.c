#include <stdio.h>
#include <pthread.h>
#include <stdint.h>

#include "../app/tpool.h"

void task(void *arg)
{
    printf("Thread #%u working on %d\n", pthread_self(), arg);
}

int main()
{

    puts("Making threadpool with 4 threads");
    threadpool thpool = thread_pool_init(4);

    puts("Adding 40 tasks to threadpool");
    int i;
    for (i = 0; i < 40; i++)
    {
        thread_pool_add_work(thpool, task, (void *)(uintptr_t)i);
    };

    thread_pool_wait(thpool);
    puts("Killing threadpool");
    thread_pool_destroy(thpool);

    return 0;
}
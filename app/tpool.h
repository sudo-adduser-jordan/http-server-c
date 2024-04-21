#ifndef _TPOOL_
#define _TPOOL_

#ifdef __cplusplus
extern "C"
{
#endif

    /* =================================== API ======================================= */

    typedef struct tpool_ *threadpool;

    /**
     * @brief  Initialize threadpool
     *
     * Initializes a threadpool. This function will not return until all
     * threads have initialized successfully.
     *
     * @example
     *
     *    ..
     *    threadpool tpool;                     //First we declare a threadpool
     *    tpool = tpool_init(4);               //then we initialize it to 4 threads
     *    ..
     *
     * @param  num_threads   number of threads to be created in the threadpool
     * @return threadpool    created threadpool on success,
     *                       NULL on error
     */
    threadpool tpool_init(int num_threads);

    /**
     * @brief Add work to the job queue
     *
     * Takes an action and its argument and adds it to the threadpool's job queue.
     * If you want to add to work a function with more than one arguments then
     * a way to implement this is by passing a pointer to a structure.
     *
     * NOTICE: You have to cast both the function and argument to not get warnings.
     *
     * @example
     *
     *    void print_num(int num){
     *       printf("%d\n", num);
     *    }
     *
     *    int main() {
     *       ..
     *       int a = 10;
     *       tpool_add_work(tpool, (void*)print_num, (void*)a);
     *       ..
     *    }
     *
     * @param  threadpool    threadpool to which the work will be added
     * @param  function_p    pointer to function to add as work
     * @param  arg_p         pointer to an argument
     * @return 0 on success, -1 otherwise.
     */
    int tpool_add_work(threadpool, void (*function_p)(void *), void *arg_p);

    /**
     * @brief Wait for all queued jobs to finish
     *
     * Will wait for all jobs - both queued and currently running to finish.
     * Once the queue is empty and all work has completed, the calling thread
     * (probably the main program) will continue.
     *
     * Smart polling is used in wait. The polling is initially 0 - meaning that
     * there is virtually no polling at all. If after 1 seconds the threads
     * haven't finished, the polling interval starts growing exponentially
     * until it reaches max_secs seconds. Then it jumps down to a maximum polling
     * interval assuming that heavy processing is being used in the threadpool.
     *
     * @example
     *
     *    ..
     *    threadpool tpool = tpool_init(4);
     *    ..
     *    // Add a bunch of work
     *    ..
     *    tpool_wait(tpool);
     *    puts("All added work has finished");
     *    ..
     *
     * @param threadpool     the threadpool to wait for
     * @return nothing
     */
    void tpool_wait(threadpool);

    /**
     * @brief Pauses all threads immediately
     *
     * The threads will be paused no matter if they are idle or working.
     * The threads return to their previous states once tpool_resume
     * is called.
     *
     * While the thread is being paused, new work can be added.
     *
     * @example
     *
     *    threadpool tpool = tpool_init(4);
     *    tpool_pause(tpool);
     *    ..
     *    // Add a bunch of work
     *    ..
     *    tpool_resume(tpool); // Let the threads start their magic
     *
     * @param threadpool    the threadpool where the threads should be paused
     * @return nothing
     */
    void tpool_pause(threadpool);

    /**
     * @brief Unpauses all threads if they are paused
     *
     * @example
     *    ..
     *    tpool_pause(tpool);
     *    sleep(10);              // Delay execution 10 seconds
     *    tpool_resume(tpool);
     *    ..
     *
     * @param threadpool     the threadpool where the threads should be unpaused
     * @return nothing
     */
    void tpool_resume(threadpool);

    /**
     * @brief Destroy the threadpool
     *
     * This will wait for the currently active threads to finish and then 'kill'
     * the whole threadpool to free up memory.
     *
     * @example
     * int main() {
     *    threadpool tpool1 = tpool_init(2);
     *    threadpool tpool2 = tpool_init(2);
     *    ..
     *    tpool_destroy(tpool1);
     *    ..
     *    return 0;
     * }
     *
     * @param threadpool     the threadpool to destroy
     * @return nothing
     */
    void tpool_destroy(threadpool);

    /**
     * @brief Show currently working threads
     *
     * Working threads are the threads that are performing work (not idle).
     *
     * @example
     * int main() {
     *    threadpool tpool1 = tpool_init(2);
     *    threadpool tpool2 = tpool_init(2);
     *    ..
     *    printf("Working threads: %d\n", tpool_num_threads_working(tpool1));
     *    ..
     *    return 0;
     * }
     *
     * @param threadpool     the threadpool of interest
     * @return integer       number of threads working
     */
    int tpool_num_threads_working(threadpool);

#ifdef __cplusplus
}
#endif

#endif
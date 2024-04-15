
/* =================================== API ======================================= */

typedef struct ThreadPool *threadpool;

/**
 * @brief  Initialize threadpool
 *
 * Initializes a threadpool. This function will not return until all
 * threads have initialized successfully.
 *
 * @example
 *
 *    ..
 *    threadpool thread_pool;                     //First we declare a threadpool
 *    thread_pool = thread_pool_init(4);               //then we initialize it to 4 threads
 *    ..
 *
 * @param  num_threads   number of threads to be created in the threadpool
 * @return threadpool    created threadpool on success,
 *                       NULL on error
 */
threadpool thread_pool_init(int num_threads);

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
 *       thread_pool_add_work(thread_pool, (void*)print_num, (void*)a);
 *       ..
 *    }
 *
 * @param  threadpool    threadpool to which the work will be added
 * @param  function_p    pointer to function to add as work
 * @param  arg_p         pointer to an argument
 * @return 0 on success, -1 otherwise.
 */
int thread_pool_add_work(threadpool, void (*function)(void *), void *arg);

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
 *    threadpool thread_pool = thread_pool_init(4);
 *    ..
 *    // Add a bunch of work
 *    ..
 *    thread_pool_wait(thread_pool);
 *    puts("All added work has finished");
 *    ..
 *
 * @param threadpool     the threadpool to wait for
 * @return nothing
 */
void thread_pool_wait(threadpool);

/**
 * @brief Pauses all threads immediately
 *
 * The threads will be paused no matter if they are idle or working.
 * The threads return to their previous states once thread_pool_resume
 * is called.
 *
 * While the thread is being paused, new work can be added.
 *
 * @example
 *
 *    threadpool thread_pool = thread_pool_init(4);
 *    thread_pool_pause(thread_pool);
 *    ..
 *    // Add a bunch of work
 *    ..
 *    thread_pool_resume(thread_pool); // Let the threads start their magic
 *
 * @param threadpool    the threadpool where the threads should be paused
 * @return nothing
 */
void thread_pool_pause(threadpool);

/**
 * @brief Unpauses all threads if they are paused
 *
 * @example
 *    ..
 *    thread_pool_pause(thread_pool);
 *    sleep(10);              // Delay execution 10 seconds
 *    thread_pool_resume(thread_pool);
 *    ..
 *
 * @param threadpool     the threadpool where the threads should be unpaused
 * @return nothing
 */
void thread_pool_resume(threadpool);

/**
 * @brief Destroy the threadpool
 *
 * This will wait for the currently active threads to finish and then 'kill'
 * the whole threadpool to free up memory.
 *
 * @example
 * int main() {
 *    threadpool thread_pool1 = thread_pool_init(2);
 *    threadpool thread_pool2 = thread_pool_init(2);
 *    ..
 *    thread_pool_destroy(thread_pool1);
 *    ..
 *    return 0;
 * }
 *
 * @param threadpool     the threadpool to destroy
 * @return nothing
 */
void thread_pool_destroy(threadpool);

/**
 * @brief Show currently working threads
 *
 * Working threads are the threads that are performing work (not idle).
 *
 * @example
 * int main() {
 *    threadpool thread_pool1 = thread_pool_init(2);
 *    threadpool thread_pool2 = thread_pool_init(2);
 *    ..
 *    printf("Working threads: %d\n", thread_pool_num_threads_working(thread_pool1));
 *    ..
 *    return 0;
 * }
 *
 * @param threadpool     the threadpool of interest
 * @return integer       number of threads working
 */
int thread_pool_num_threads_working(threadpool);

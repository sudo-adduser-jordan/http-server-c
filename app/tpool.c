
// Verify definitions - TODO
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500
#endif

#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <sys/prctl.h>

#include "tpool.h"

/* Binary semaphore */
typedef struct BinarySemapore
{
    int counter;
    pthread_cond_t condition;
    pthread_mutex_t mutex;
} BinarySemapore;

typedef struct Job
{
    void *arg;                   /* function's argument       */
    void (*function)(void *arg); /* function pointer          */
    struct Job *previous;        /* pointer to previous Job   */
} Job;

typedef struct JobQueue
{
    int len;                          /* number of jobs in queue   */
    Job *front;                       /* pointer to front of queue */
    Job *rear;                        /* pointer to rear  of queue */
    BinarySemapore *has_jobs;         /* flag as binary semaphore  */
    pthread_mutex_t read_write_mutex; /* used for queue r/w access */
} JobQueue;

typedef struct Thread
{
    int thread_id;                  /* thread id               */
    pthread_t pthread;              /* pointer to actual thread  */
    struct ThreadPool *thread_pool; /* access to thpool          */
} Thread;

typedef struct ThreadPool
{
    Thread **threads;                  /* pointer to threads        */
    volatile int num_threads_alive;    /* threads currently alive   */
    volatile int num_threads_working;  /* threads currently working */
    pthread_mutex_t thread_count_lock; /* used for thread count etc */
    pthread_cond_t threads_all_idle;   /* signal to ThreadPoolwait     */
    JobQueue job_queue;                /* job queue                 */
} ThreadPool;

static volatile int threads_keepalive;
static volatile int threads_on_hold;

/* ========================== THREADPOOL ============================ */

static int thread_init(ThreadPool *thread_pool, struct Thread **thread, int id);
static void *thread_do(struct Thread *thread);
static void thread_hold(int sig_id);
static void thread_destroy(struct Thread *thread);

static int job_queue_init(JobQueue *job_queue);
static void job_queue_clear(JobQueue *job_queue);
static void job_queue_push(JobQueue *job_queue, struct Job *new_job);
static struct Job *job_queue_pull(JobQueue *job_queue);
static void job_queue_destroy(JobQueue *job_queue);

static void bsem_init(struct BinarySemapore *bsem, int value);
static void bsem_reset(struct BinarySemapore *bsem);
static void bsem_post(struct BinarySemapore *bsem);
static void bsem_post_all(struct BinarySemapore *bsem);
static void bsem_wait(struct BinarySemapore *bsem);

/* ========================== THREADPOOL ============================ */

/* Initialise thread pool */
struct ThreadPool *thread_pool_init(int num_threads)
{

    threads_on_hold = 0;
    threads_keepalive = 1;

    if (num_threads < 0)
    {
        num_threads = 0;
    }

    /* Make new thread pool */
    ThreadPool *thread_pool;
    thread_pool = (struct ThreadPool *)malloc(sizeof(struct ThreadPool));
    if (thread_pool == NULL)
    {
        perror("thread_pool_init(): Could not allocate memory for thread pool\n");
        return NULL;
    }
    thread_pool->num_threads_alive = 0;
    thread_pool->num_threads_working = 0;

    /* Initialise the job queue */
    if (job_queue_init(&thread_pool->job_queue) == -1)
    {
        perror("thread_pool_init(): Could not allocate memory for job queue\n");
        free(thread_pool);
        return NULL;
    }

    /* Make threads in pool */
    thread_pool->threads = (struct Thread **)malloc(num_threads * sizeof(struct thread *));
    if (thread_pool->threads == NULL)
    {
        perror("thread_pool_init(): Could not allocate memory for threads\n");
        job_queue_destroy(&thread_pool->job_queue);
        free(thread_pool);
        return NULL;
    }

    pthread_mutex_init(&(thread_pool->thread_count_lock), NULL);
    pthread_cond_init(&thread_pool->threads_all_idle, NULL);

    /* Thread init */
    int n;
    for (n = 0; n < num_threads; n++)
    {
        thread_init(thread_pool, &thread_pool->threads[n], n);
        printf("Created thread %d in pool \n", n);
    }

    /* Wait for threads to initialize */
    while (thread_pool->num_threads_alive != num_threads)
    {
    }

    return thread_pool;
}

/* Add work to the thread pool */
int thread_pool_add_work(ThreadPool *thread_pool, void (*function)(void *), void *arg)
{
    Job *newjob;

    newjob = (struct Job *)malloc(sizeof(struct Job));
    if (newjob == NULL)
    {
        perror("thread_pool_add_work(): Could not allocate memory for new job\n");
        return -1;
    }

    /* add function and argument */
    newjob->function = function;
    newjob->arg = arg;

    /* add job to queue */
    job_queue_push(&thread_pool->job_queue, newjob);

    return 0;
}

/* Wait until all jobs have finished */
void thread_pool_wait(ThreadPool *thread_pool)
{
    pthread_mutex_lock(&thread_pool->thread_count_lock);
    while (thread_pool->job_queue.len || thread_pool->num_threads_working)
    {
        pthread_cond_wait(&thread_pool->threads_all_idle, &thread_pool->thread_count_lock);
    }
    pthread_mutex_unlock(&thread_pool->thread_count_lock);
}

/* Destroy the threadpool */
void thread_pool_destroy(ThreadPool *thread_pool)
{
    /* No need to destroy if it's NULL */
    if (thread_pool == NULL)
        return;

    volatile int threads_total = thread_pool->num_threads_alive;

    /* End each thread 's infinite loop */
    threads_keepalive = 0;

    /* Give one second to kill idle threads */
    double TIMEOUT = 1.0;
    time_t start, end;
    double tpassed = 0.0;
    time(&start);
    while (tpassed < TIMEOUT && thread_pool->num_threads_alive)
    {
        bsem_post_all(thread_pool->job_queue.has_jobs);
        time(&end);
        tpassed = difftime(end, start);
    }

    /* Poll remaining threads */
    while (thread_pool->num_threads_alive)
    {
        bsem_post_all(thread_pool->job_queue.has_jobs);
        sleep(1);
    }

    /* Job queue cleanup */
    job_queue_destroy(&thread_pool->job_queue);
    /* Deallocs */
    int n;
    for (n = 0; n < threads_total; n++)
    {
        thread_destroy(thread_pool->threads[n]);
    }
    free(thread_pool->threads);
    free(thread_pool);
}

/* ============================ THREAD ============================== */

/* Initialize a thread in the thread pool
 *
 * @param thread        address to the pointer of the thread to be created
 * @param id            id to be given to the thread
 * @return 0 on success, -1 otherwise.
 */
static int thread_init(ThreadPool *thread_pool, struct Thread **thread, int id)
{

    *thread = (struct Thread *)malloc(sizeof(struct Thread));
    if (*thread == NULL)
    {
        perror("thread_init(): Could not allocate memory for thread\n");
        return -1;
    }

    (*thread)->thread_pool = thread_pool;
    (*thread)->thread_id = id;

    pthread_create(&(*thread)->pthread, NULL, (void *(*)(void *))thread_do, (*thread));
    pthread_detach((*thread)->pthread);
    return 0;
}

/* Sets the calling thread on hold */
static void thread_hold(int sig_id)
{
    (void)sig_id;
    threads_on_hold = 1;
    while (threads_on_hold)
    {
        sleep(1);
    }
}

/* What each thread is doing
 *
 * In principle this is an endless loop. The only time this loop gets interrupted is once
 * thpool_destroy() is invoked or the program exits.
 *
 * @param  thread        thread that will run this function
 * @return nothing
 */
static void *thread_do(struct Thread *thread)
{

    /* Set thread name for profiling and debugging */
    char thread_name[16] = {0};

    // snprintf(thread_name, 16, TOSTRING("THPOOL_THREAD_NAME") "-%d", thread->thread_id);
    // snprintf(thread_name, 16, TOSTRING(THPOOL_THREAD_NAME) "-%d", thread_p->id);

    /* Use prctl instead to prevent using _GNU_SOURCE flag and implicit declaration */
    prctl(PR_SET_NAME, thread_name);

    /* Assure all threads have been created before starting serving */
    ThreadPool *thread_pool = thread->thread_pool;

    /* Register signal handler */
    struct sigaction act;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_ONSTACK;
    act.sa_handler = thread_hold;
    if (sigaction(SIGUSR1, &act, NULL) == -1)
    {
        perror("thread_do(): cannot handle SIGUSR1");
    }

    /* Mark thread as alive (initialized) */
    pthread_mutex_lock(&thread_pool->thread_count_lock);
    thread_pool->num_threads_alive += 1;
    pthread_mutex_unlock(&thread_pool->thread_count_lock);

    while (threads_keepalive)
    {

        bsem_wait(thread_pool->job_queue.has_jobs);

        if (threads_keepalive)
        {

            pthread_mutex_lock(&thread_pool->thread_count_lock);
            thread_pool->num_threads_working++;
            pthread_mutex_unlock(&thread_pool->thread_count_lock);

            /* Read job from queue and execute it */
            void (*func_buff)(void *);
            void *arg_buff;
            Job *job = job_queue_pull(&thread_pool->job_queue);
            if (job)
            {
                func_buff = job->function;
                arg_buff = job->arg;
                func_buff(arg_buff);
                free(job);
            }

            pthread_mutex_lock(&thread_pool->thread_count_lock);
            thread_pool->num_threads_working--;
            if (!thread_pool->num_threads_working)
            {
                pthread_cond_signal(&thread_pool->threads_all_idle);
            }
            pthread_mutex_unlock(&thread_pool->thread_count_lock);
        }
    }
    pthread_mutex_lock(&thread_pool->thread_count_lock);
    thread_pool->num_threads_alive--;
    pthread_mutex_unlock(&thread_pool->thread_count_lock);

    return NULL;
}

/* Frees a thread  */
static void thread_destroy(Thread *thread_p)
{
    free(thread_p);
}

/* ============================ JOB QUEUE =========================== */

/* Initialize queue */
static int job_queue_init(JobQueue *job_queue)
{
    job_queue->len = 0;
    job_queue->front = NULL;
    job_queue->rear = NULL;

    job_queue->has_jobs = (struct BinarySemapore *)malloc(sizeof(struct BinarySemapore));
    if (job_queue->has_jobs == NULL)
    {
        return -1;
    }

    pthread_mutex_init(&(job_queue->read_write_mutex), NULL);
    bsem_init(job_queue->has_jobs, 0);

    return 0;
}

/* Clear the queue */
static void job_queue_clear(JobQueue *job_queue)
{

    while (job_queue->len)
    {
        free(job_queue_pull(job_queue));
    }

    job_queue->front = NULL;
    job_queue->rear = NULL;
    bsem_reset(job_queue->has_jobs);
    job_queue->len = 0;
}

/* Add (allocated) job to queue
 */
static void job_queue_push(JobQueue *job_queue, struct Job *newjob)
{

    pthread_mutex_lock(&job_queue->read_write_mutex);
    newjob->previous = NULL;

    switch (job_queue->len)
    {

    case 0: /* if no jobs in queue */
        job_queue->front = newjob;
        job_queue->rear = newjob;
        break;

    default: /* if jobs in queue */
        job_queue->rear->previous = newjob;
        job_queue->rear = newjob;
    }
    job_queue->len++;

    bsem_post(job_queue->has_jobs);
    pthread_mutex_unlock(&job_queue->read_write_mutex);
}

/* Get first job from queue(removes it from queue)
 * Notice: Caller MUST hold a mutex
 */
static struct Job *job_queue_pull(JobQueue *job_queue)
{

    pthread_mutex_lock(&job_queue->read_write_mutex);
    Job *job = job_queue->front;

    switch (job_queue->len)
    {

    case 0: /* if no jobs in queue */
        break;

    case 1: /* if one job in queue */
        job_queue->front = NULL;
        job_queue->rear = NULL;
        job_queue->len = 0;
        break;

    default: /* if >1 jobs in queue */
        job_queue->front = job->previous;
        job_queue->len--;
        /* more than one job in queue -> post it */
        bsem_post(job_queue->has_jobs);
    }

    pthread_mutex_unlock(&job_queue->read_write_mutex);
    return job;
}

/* Free all queue resources back to the system */
static void job_queue_destroy(JobQueue *job_queue)
{
    job_queue_clear(job_queue);
    free(job_queue->has_jobs);
}

/* ======================== SYNCHRONISATION ========================= */

/* Init semaphore to 1 or 0 */
static void bsem_init(BinarySemapore *bsem, int value)
{
    if (value < 0 || value > 1)
    {
        perror("bsem_init(): Binary semaphore can take only values 1 or 0");
        exit(1);
    }
    pthread_mutex_init(&(bsem->mutex), NULL);
    pthread_cond_init(&(bsem->condition), NULL);
    bsem->counter = value;
}

/* Reset semaphore to 0 */
static void bsem_reset(BinarySemapore *bsem_p)
{
    pthread_mutex_destroy(&(bsem_p->mutex));
    pthread_cond_destroy(&(bsem_p->condition));
    bsem_init(bsem_p, 0);
}

/* Post to at least one thread */
static void bsem_post(BinarySemapore *bsem)
{
    pthread_mutex_lock(&bsem->mutex);
    bsem->counter = 1;
    pthread_cond_signal(&bsem->condition);
    pthread_mutex_unlock(&bsem->mutex);
}

/* Post to all threads */
static void bsem_post_all(BinarySemapore *bsem)
{
    pthread_mutex_lock(&bsem->mutex);
    bsem->counter = 1;
    pthread_cond_broadcast(&bsem->condition);
    pthread_mutex_unlock(&bsem->mutex);
}

/* Wait on semaphore until semaphore has value 0 */
static void bsem_wait(BinarySemapore *bsem)
{
    pthread_mutex_lock(&bsem->mutex);
    while (bsem->counter != 1)
    {
        pthread_cond_wait(&bsem->condition, &bsem->mutex);
    }
    bsem->counter = 0;
    pthread_mutex_unlock(&bsem->mutex);
}
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>

#include "thread_pool.h"

static void *pool_thread_server(void *arg);

/*********************************************************************
*功能:      初始化线程池结构体并创建线程
*参数:      
            pool：线程池句柄
            threads_limit：线程池中线程的数量
*返回值:    无
*********************************************************************/
void pool_init(pool_t *pool, int threads_limit)
{
    pthread_attr_t attr;
    
    pool->threads_limit = threads_limit;
    pool->queue_head    = NULL;
    pool->task_in_queue = 0;
    pool->destroy_flag  = 0;
    
    /*创建存放线程ID的空间*/
    pool->threadid = (pthread_t *)calloc(threads_limit, sizeof(pthread_t));
    
    int i = 0;
    
    /*初始化互斥锁和条件变量*/
    pthread_mutex_init(&(pool->queue_lock), NULL);
    pthread_cond_init(&(pool->queue_ready), NULL);
    
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 4194304);
    
    /*循环创建threads_limit个线程*/
    for (i = 0; i < threads_limit; i++)
    {
        pthread_create(&(pool->threadid[i]), &attr, pool_thread_server, pool);
    }
    return;
}

/*********************************************************************
*功能:      销毁线程池，等待队列中的任务不会再被执行，
            但是正在运行的线程会一直,把任务运行完后再退出
*参数:      线程池句柄
*返回值:    成功：0，失败非0
*********************************************************************/
int pool_uninit(pool_t *pool)
{
    pool_task *head = NULL;
    int i;
    
    pthread_mutex_lock(&(pool->queue_lock));
    if(pool->destroy_flag)/* 防止两次调用 */
        return -1;
    
    pool->destroy_flag = 1;
    pthread_mutex_unlock(&(pool->queue_lock));
    
    /* 唤醒所有等待线程，线程池要销毁了 */
    pthread_cond_broadcast(&(pool->queue_ready));
    
    /* 阻塞等待线程退出，否则就成僵尸了 */
    for (i = 0; i < pool->threads_limit; i++)
        pthread_join(pool->threadid[i], NULL);
    free(pool->threadid);
    
    /* 销毁等待队列 */
    pthread_mutex_lock(&(pool->queue_lock));
    while(pool->queue_head != NULL){
        head = pool->queue_head;
        pool->queue_head = pool->queue_head->next;
        free(head);
    }
    pthread_mutex_unlock(&(pool->queue_lock));
    
    /*条件变量和互斥量也别忘了销毁*/
    pthread_mutex_destroy(&(pool->queue_lock));
    pthread_cond_destroy(&(pool->queue_ready));
    
    return 0;
}

/*********************************************************************
*功能:      向任务队列中添加一个任务
*参数:      
            pool：线程池句柄
            process：任务处理函数
            arg：任务参数
*返回值:    无
*********************************************************************/
static void enqueue_task(pool_t *pool, pool_task_f process, int arg)
{
    pool_task *task = NULL;
    pool_task *member = NULL;
    
    pthread_mutex_lock(&(pool->queue_lock));
    if(pool->task_in_queue >= pool->threads_limit){
        printf("task_in_queue > threads_limit!\n");
        pthread_mutex_unlock (&(pool->queue_lock));
        return;
    }
    
    task = (pool_task *)calloc(1, sizeof(pool_task));
    //assert(task != NULL);
    task->process = process;
    task->arg     = arg;
    task->next    = NULL;
    pool->task_in_queue++;
    
    member = pool->queue_head;
    if(member != NULL)
    {
        while(member->next != NULL) /* 将任务加入到任务链连的最后位置. */
            member = member->next;
        member->next = task;
    }
    else
    {
        pool->queue_head = task;    /* 如果是第一个任务的话,就指向头 */
    }
    
    //printf("\ttasks %d\n", pool->task_in_queue);
    /* 等待队列中有任务了，唤醒一个等待线程 */
    pthread_cond_signal(&(pool->queue_ready));
    pthread_mutex_unlock(&(pool->queue_lock));
}

/*********************************************************************
*功能:      从任务队列中取出一个任务
*参数:      线程池句柄
*返回值:    任务句柄
*********************************************************************/
static pool_task *dequeue_task(pool_t *pool)
{
    pool_task *task = NULL;
    
    pthread_mutex_lock(&(pool->queue_lock));
    /* 判断线程池是否要销毁了 */
    if(pool->destroy_flag)
    {
        pthread_mutex_unlock(&(pool->queue_lock));
        printf("thread 0x%lx will be destroyed\n", pthread_self());
        pthread_exit(NULL);
    }
    
    /* 如果等待队列为0并且不销毁线程池，则处于阻塞状态 */
    if(pool->task_in_queue == 0)
    {
        while((pool->task_in_queue == 0) && (!pool->destroy_flag)){
            //printf("thread 0x%lx is leisure\n", pthread_self());
            /* 注意:pthread_cond_wait是一个原子操作，等待前会解锁，唤醒后会加锁 */
            pthread_cond_wait(&(pool->queue_ready), &(pool->queue_lock));
        }
    }
    else
    {
        /* 等待队列长度减去1，并取出队列中的第一个元素 */
        pool->task_in_queue--;
        task = pool->queue_head;
        pool->queue_head = task->next;
        //printf("thread 0x%lx received a task\n", pthread_self());
    }
    pthread_mutex_unlock(&(pool->queue_lock));
    
    return task;
}

/*********************************************************************
*功能:      向线程池中添加一个任务
*参数:      
            pool：线程池句柄
            process：任务处理函数
            arg：任务参数
*返回值:    0
*********************************************************************/
int pool_add_task(pool_t *pool, pool_task_f process, int arg)
{
    enqueue_task(pool, process, arg);
    return 0;
}

/*********************************************************************
*功能:      线程池服务程序
*参数:      略
*返回值:    略
*********************************************************************/
static void *pool_thread_server(void *arg)
{
    pool_t *pool = NULL;
    
    pool = (pool_t *)arg;
    while(1)
    {
        pool_task *task = NULL;
        task = dequeue_task(pool);
        /*调用回调函数，执行任务*/
        if(task != NULL)
        {
            //printf ("thread 0x%lx is busy\n", pthread_self());
            task->process(task->arg);
            free(task);
            task = NULL;
        }
    }
    
    /*这一句应该是不可达的*/
    pthread_exit(NULL);
    
    return NULL;
}

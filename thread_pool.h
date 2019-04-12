#ifndef __THREAD_POOL_H__
#define __THREAD_POOL_H__

#include <pthread.h>

/*********************************************************************
* 任务回调函数，也可根据需要自行修改
*********************************************************************/
typedef void (*pool_task_f)(int arg);

/*********************************************************************
* 任务句柄
*********************************************************************/
typedef struct _task
{
    pool_task_f process;    /*回调函数，任务运行时会调用此函数，注意也可声明成其它形式*/
    int arg;                /*回调函数的参数*/
    struct _task *next;
}pool_task;

/*********************************************************************
* 线程池句柄
*********************************************************************/
typedef struct
{
    pthread_t *threadid;        /* 线程号 */
    int threads_limit;          /* 线程池中允许的活动线程数目 */
    int destroy_flag;           /* 是否销毁线程池 , 0销毁，1不销毁*/
    pool_task *queue_head;      /* 链表结构，线程池中所有等待任务 */
    int task_in_queue;          /* 当前等待队列的任务数目 */
    pthread_mutex_t queue_lock; /* 锁 */
    pthread_cond_t queue_ready; /* 条件变量 */
}pool_t;

/*********************************************************************
*功能:      初始化线程池结构体并创建线程
*参数:      
            pool：线程池句柄
            threads_limit：线程池中线程的数量
*返回值:    无
*********************************************************************/
void pool_init(pool_t *pool, int threads_limit);

/*********************************************************************
*功能:      销毁线程池，等待队列中的任务不会再被执行，
            但是正在运行的线程会一直,把任务运行完后再退出
*参数:      线程池句柄
*返回值:    成功：0，失败非0
*********************************************************************/
int pool_uninit(pool_t *pool);

/*********************************************************************
*功能:      向线程池中添加一个任务
*参数:      
            pool：线程池句柄
            process：任务处理函数
            arg：任务参数
*返回值:    0
*********************************************************************/
int pool_add_task(pool_t *pool, pool_task_f process, int arg);

#endif

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>

#include "thread_pool.h"

static void *pool_thread_server(void *arg);

/*********************************************************************
*����:      ��ʼ���̳߳ؽṹ�岢�����߳�
*����:      
            pool���̳߳ؾ��
            threads_limit���̳߳����̵߳�����
*����ֵ:    ��
*********************************************************************/
void pool_init(pool_t *pool, int threads_limit)
{
    pthread_attr_t attr;
    
    pool->threads_limit = threads_limit;
    pool->queue_head    = NULL;
    pool->task_in_queue = 0;
    pool->destroy_flag  = 0;
    
    /*��������߳�ID�Ŀռ�*/
    pool->threadid = (pthread_t *)calloc(threads_limit, sizeof(pthread_t));
    
    int i = 0;
    
    /*��ʼ������������������*/
    pthread_mutex_init(&(pool->queue_lock), NULL);
    pthread_cond_init(&(pool->queue_ready), NULL);
    
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 4194304);
    
    /*ѭ������threads_limit���߳�*/
    for (i = 0; i < threads_limit; i++)
    {
        pthread_create(&(pool->threadid[i]), &attr, pool_thread_server, pool);
    }
    return;
}

/*********************************************************************
*����:      �����̳߳أ��ȴ������е����񲻻��ٱ�ִ�У�
            �����������е��̻߳�һֱ,����������������˳�
*����:      �̳߳ؾ��
*����ֵ:    �ɹ���0��ʧ�ܷ�0
*********************************************************************/
int pool_uninit(pool_t *pool)
{
    pool_task *head = NULL;
    int i;
    
    pthread_mutex_lock(&(pool->queue_lock));
    if(pool->destroy_flag)/* ��ֹ���ε��� */
        return -1;
    
    pool->destroy_flag = 1;
    pthread_mutex_unlock(&(pool->queue_lock));
    
    /* �������еȴ��̣߳��̳߳�Ҫ������ */
    pthread_cond_broadcast(&(pool->queue_ready));
    
    /* �����ȴ��߳��˳�������ͳɽ�ʬ�� */
    for (i = 0; i < pool->threads_limit; i++)
        pthread_join(pool->threadid[i], NULL);
    free(pool->threadid);
    
    /* ���ٵȴ����� */
    pthread_mutex_lock(&(pool->queue_lock));
    while(pool->queue_head != NULL){
        head = pool->queue_head;
        pool->queue_head = pool->queue_head->next;
        free(head);
    }
    pthread_mutex_unlock(&(pool->queue_lock));
    
    /*���������ͻ�����Ҳ����������*/
    pthread_mutex_destroy(&(pool->queue_lock));
    pthread_cond_destroy(&(pool->queue_ready));
    
    return 0;
}

/*********************************************************************
*����:      ��������������һ������
*����:      
            pool���̳߳ؾ��
            process����������
            arg���������
*����ֵ:    ��
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
        while(member->next != NULL) /* ��������뵽�������������λ��. */
            member = member->next;
        member->next = task;
    }
    else
    {
        pool->queue_head = task;    /* ����ǵ�һ������Ļ�,��ָ��ͷ */
    }
    
    //printf("\ttasks %d\n", pool->task_in_queue);
    /* �ȴ��������������ˣ�����һ���ȴ��߳� */
    pthread_cond_signal(&(pool->queue_ready));
    pthread_mutex_unlock(&(pool->queue_lock));
}

/*********************************************************************
*����:      �����������ȡ��һ������
*����:      �̳߳ؾ��
*����ֵ:    ������
*********************************************************************/
static pool_task *dequeue_task(pool_t *pool)
{
    pool_task *task = NULL;
    
    pthread_mutex_lock(&(pool->queue_lock));
    /* �ж��̳߳��Ƿ�Ҫ������ */
    if(pool->destroy_flag)
    {
        pthread_mutex_unlock(&(pool->queue_lock));
        printf("thread 0x%lx will be destroyed\n", pthread_self());
        pthread_exit(NULL);
    }
    
    /* ����ȴ�����Ϊ0���Ҳ������̳߳أ���������״̬ */
    if(pool->task_in_queue == 0)
    {
        while((pool->task_in_queue == 0) && (!pool->destroy_flag)){
            //printf("thread 0x%lx is leisure\n", pthread_self());
            /* ע��:pthread_cond_wait��һ��ԭ�Ӳ������ȴ�ǰ����������Ѻ����� */
            pthread_cond_wait(&(pool->queue_ready), &(pool->queue_lock));
        }
    }
    else
    {
        /* �ȴ����г��ȼ�ȥ1����ȡ�������еĵ�һ��Ԫ�� */
        pool->task_in_queue--;
        task = pool->queue_head;
        pool->queue_head = task->next;
        //printf("thread 0x%lx received a task\n", pthread_self());
    }
    pthread_mutex_unlock(&(pool->queue_lock));
    
    return task;
}

/*********************************************************************
*����:      ���̳߳������һ������
*����:      
            pool���̳߳ؾ��
            process����������
            arg���������
*����ֵ:    0
*********************************************************************/
int pool_add_task(pool_t *pool, pool_task_f process, int arg)
{
    enqueue_task(pool, process, arg);
    return 0;
}

/*********************************************************************
*����:      �̳߳ط������
*����:      ��
*����ֵ:    ��
*********************************************************************/
static void *pool_thread_server(void *arg)
{
    pool_t *pool = NULL;
    
    pool = (pool_t *)arg;
    while(1)
    {
        pool_task *task = NULL;
        task = dequeue_task(pool);
        /*���ûص�������ִ������*/
        if(task != NULL)
        {
            //printf ("thread 0x%lx is busy\n", pthread_self());
            task->process(task->arg);
            free(task);
            task = NULL;
        }
    }
    
    /*��һ��Ӧ���ǲ��ɴ��*/
    pthread_exit(NULL);
    
    return NULL;
}

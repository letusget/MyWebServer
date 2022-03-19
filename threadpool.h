#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <list>
#include <cstdio>
#include <iostream>
#include "condition.h"
#include "locker.h"
#include "sem.h"

//线程池的封装类

//这里使用模板类，方便代码复用, 这里T 即为任务类
template<typename T>
class threadpool
{
private:
    
    //线程数量
    int m_thread_number;

    //线程池数组, 大小为线程的数量,m_thread_number
    pthread_t * m_threads;

    //请求队列中，最多允许的等待处理的请求数量
    int m_max_request;

    //请求队列
    std::list< T* > m_workqueue;

    //互斥锁
    locker m_queueLocker;

    //信号量 -> 判断是否有任务需要处理
    sem m_queueStat;

    //时候结束线程
    bool m_stop;

    //静态函数，创建线程要执行的操作
    static void* worker(void * arg);

    //启动线程池
    void run();

public:
    //有参构造：这里默认最大线程数量为8，最大请求数量为10000
    threadpool(int thread_number = 8,int max_requests = 10000);

    //添加任务
    bool append(T* request);

    //析构函数
    ~threadpool();
};


#endif
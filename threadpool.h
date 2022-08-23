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

    //是否结束线程
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


//#include "threadpool.h"

//使用成员初始化
template<typename T>
threadpool<T>::threadpool(int thread_number,int max_requests):
m_thread_number(thread_number), m_max_request(max_requests),
m_stop(false),m_threads(nullptr)
{
    //排除异常情况
    if(thread_number <= 0 || max_requests <= 0)
    {
        throw std::exception();
    }

    //线程池数组, 要记得手动释放
    m_threads = new pthread_t[m_thread_number];
    if(!m_threads)
    {
        //创建失败就抛出异常
        throw std::exception();
    }

    //创建thread_number个线程，并为其创建线程分离, 方便回收线程资源
    for(int i=0;i<thread_number;i++)
    {
        //提示创建的线程数量
        printf("create the %dth thread\n",i+1);

        //使用this 作为传入传出参数，这样就可以实现 静态变量 访问 非静态的成员了
        if( pthread_create(m_threads+i,nullptr,worker,this) != 0)       
        {
            //创建失败就释放资源, 抛出异常
            delete [] m_threads;
            throw std::exception();
        } 

        //创建成功，设置线程分离
        if( pthread_detach(m_threads[i]) ) 
        {
            //如果分离失败就抛出异常
            delete [] m_threads;
            throw std::exception();
        }

    }


}

//添加任务
template<typename T>
bool threadpool<T>::append(T* request)
{
    //加锁
    m_queueLocker.lock();
    
    if(m_workqueue.size()>m_max_request)
    {
        //解锁 并且返回错误
        m_queueLocker.unlock();
        return false;
    }

    //添加任务
    m_workqueue.push_back(request);
    //解锁
    m_queueLocker.unlock();
    //信号量增加
    m_queueStat.post();
    //判断依据
    return true;
}

//信号执行函数
template<typename T>
void * threadpool<T>::worker(void * arg)
{
    threadpool * pool = (threadpool*) arg;
    //执行线程池
    pool->run();
    
    return pool;

}

template<typename T>
void threadpool<T>::run()
{
    //一直运行到m_stop为true才停止
    while (!m_stop)
    {
        //寻找任务
        m_queueStat.wait();
        //加锁
        m_queueLocker.lock();

        //队列空了就解锁
        if(m_workqueue.empty())
        {
            m_queueLocker.unlock();
            //继续这个循环，直到有数据
            continue;
        }

        //获取第一个任务
        T* request = m_workqueue.front();
        //从任务队列中去掉这个任务
        m_workqueue.pop_front();
        m_queueLocker.unlock();

        //防止没有获取到
        if(!request)
        {
            //继续等待任务
            continue;
        }

        //运行该任务
        request->process();
        
        
    }
    

}

//析构函数
template<typename T>
threadpool<T>::~threadpool()
{
    //释放资源
    delete[] m_threads;
    m_stop=true;

}


#endif
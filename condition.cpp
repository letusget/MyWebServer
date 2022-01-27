#include "condition.h"

cond::cond(/* args */)
{
    if(pthread_cond_init(&m_cond,nullptr))
    {
        throw std::exception();

    }
}

//线程同步
bool cond::wait(pthread_mutex_t * mutex)
{
    return pthread_cond_wait(&m_cond,mutex) == 0;
}

//线程同步的超时处理，堵塞
bool cond::timedwait(pthread_mutex_t * mutex, struct timespec t)
{
    return pthread_cond_timedwait(&m_cond,mutex,&t) == 0;
}

//信号 唤醒某个线程
bool cond::signal(pthread_mutex_t * mutex)
{
    return pthread_cond_signal(&m_cond) == 0;
}

//唤醒所有线程
bool cond::broadcast()
{
    return pthread_cond_broadcast(&m_cond)==0;
}

cond::~cond()
{
    pthread_cond_destroy(&m_cond);
}
#include "locker.h"

//构造函数
locker::locker()
{
    if(pthread_mutex_init(&m_mutex,nullptr)!=0)
    {
        //抛出异常 对象
        throw std::exception();
    }
}

//加锁
bool locker::lock()
{
    return pthread_mutex_lock(&m_mutex)==0;
}

//解锁
bool locker::unlock()
{
    return pthread_mutex_unlock(&m_mutex)==0;   
}

//获取当前互斥量
pthread_mutex_t * locker::get()
{
    return &m_mutex;
}

//析构函数
locker::~locker()
{
    pthread_mutex_destroy(&m_mutex);
}
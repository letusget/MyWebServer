#ifndef LOCKER_H
#define LOCKER_H

#include <pthread.h>    //互斥锁需要的头文件
#include <exception>    //异常类
/*
线程同步机制封装类
*/

/*
    互斥锁类
*/
class locker
{
private:
    /* 
    互斥锁 
    */
   pthread_mutex_t m_mutex;

public:
    //构造函数
    locker();

    //加锁
    bool lock();

    //解锁
    bool unlock();

    //获取当前互斥量
    pthread_mutex_t * get();  

    //析构函数
    ~locker();
};





#endif
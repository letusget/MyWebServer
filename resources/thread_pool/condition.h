#ifndef CONDITION_H
#define CONDITION_H

#include <pthread.h>    //互斥锁需要的头文件
#include <exception>    //异常类


//条件变量类
class cond
{
private:
    /* 要操作的条件变量 */
    pthread_cond_t m_cond;
public:
    cond();

    //线程同步
    bool wait(pthread_mutex_t * mutex);

    //线程同步的超时处理，堵塞
    bool timedwait(pthread_mutex_t * mutex,struct timespec t);

    //信号 唤醒某个线程
    bool signal(pthread_mutex_t * mutex);

    //唤醒所有线程
    bool broadcast();
    
    ~cond();
};




#endif

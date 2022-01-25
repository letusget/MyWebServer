#ifndef SEM_H
#define SEM_H

#include <pthread.h>    //互斥锁需要的头文件
#include <exception>    //异常类
#include <semaphore.h>  //信号量需要的头文件

//信号类
class sem
{
private:
    /*要操作的信号量*/
    sem_t m_sem;

public:
    sem();

    //初始化指定的信号量
    sem(int num);

    //等待信号量,对信号量加锁
    bool wait();

    //增加信号量，对信号量解锁
    bool post();

    ~sem();
};


#endif
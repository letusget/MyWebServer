#include "sem.h"

sem::sem()
{
    if(sem_init(&m_sem,0,0)!=0)
    {
        //抛出异常
        throw std::exception();
    }
}

//初始化指定的信号量
sem::sem(int num)
{
    if(sem_init(&m_sem,0,num)!=0)
    {
        //抛出异常
        throw std::exception();
    }
}

//等待信号量,对信号量加锁
bool sem::wait()
{
    return sem_wait(&m_sem)==0;
}

//增加信号量，对信号量解锁
bool sem::post()
{
    return sem_post(&m_sem)==0;
}

sem::~sem()
{
    sem_destroy(&m_sem);
}
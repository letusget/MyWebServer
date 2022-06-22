#ifndef _THREADPOOL_H
#define _THREADPOOL_H

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

//需要先声明，否则在头文件中无法使用
typedef struct ThreadPool ThreadPool;


//创建线程池并初始化: 最大和最小线程数以及任务队列的容量
ThreadPool* threadPoolCreate(int min,int max,int queueSize);

//销毁线程池
int threadPoolDestroy(ThreadPool* pool);

//向线程池中添加任务
void threadPoolAdd(ThreadPool* pool, void(*func)(void*),void* arg); 

//当前线程池中线程个数
int threadPoolBusyNum(ThreadPool* pool);

//获取当前创建的线程个数
int threadPoolAliveNum(ThreadPool* pool);

/************************************************/
//工作函数
void* worker(void* arg);
void* manager(void* arg);
void threadExit(ThreadPool* pool);

#endif
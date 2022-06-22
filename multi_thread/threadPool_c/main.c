#include "threadPool.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

//工作函数
void taskFunction(void* arg)
{
    //需要对传入的地址进行类型转换，然后再解引用，得到该地址保存的数据
    int num=*(int*)arg;
    printf("thread %ld is working, number = %d \n",pthread_self(),num);

    sleep(1);
}
int main()
{
    printf("%s 向你问好!\n", "threadpool");
    int minThread=3;
    int maxThread=10;
    int tasks=100;
    //创建线程池
    ThreadPool* pool=threadPoolCreate(minThread,maxThread,tasks);

    //添加100个任务
    //使用堆 内存
    //int * num=(int *)malloc(sizeof(int));
    for(int i=0;i<100;i++)
    {
        //使用堆 内存
        int * num=(int *)malloc(sizeof(int));   //这里的空间在work中释放了，不存在内存泄露
        *num=i+100;
        threadPoolAdd(pool, taskFunction, num);
    }

    //保证工作线程已经处理完毕
    sleep(30);

    //销毁线程池
    threadPoolDestroy(pool);
    
    return 0;
}
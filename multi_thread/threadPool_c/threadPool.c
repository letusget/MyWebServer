#include "threadPool.h"

//方便制作为动态库或静态库，封装性更好，所以不放到 头文件中了

//每次新增加的线程池中 新线程的个数
const int NUMBER=2;

//任务结构体
//存储需要处理的任务，由工作的线程来处理这些任务
typedef struct Task
{
	//使用函数指针作为参数：可以适用于更多的类型
	void (*function)(void* arg);
	//实参地址
	void* arg;

}Task;

//线程池结构体
struct ThreadPool
{
	//任务队列
	Task* taskQ;

	//任务队列的容量
	int queueCapacity;

	//当前任务队列中任务的个数
	int queueSize;

	//维护任务队列
	int queueFront;	//队头 取数据
	int queueTail;	//队尾 放数据

	
	//管理者线程
	pthread_t managerID;	//管理者线程ID
	//工作者线程
	pthread_t *threadIDs;	//工作的线程ID

	//线程数: 指定线程数的范围
	int minNum;
	int maxNum;

	//当前工作的线程数
	int busyNum;
	//当前存活的线程数，已创建的线程个数
	int liveNum;
	//空闲的线程，方便在任务较少时杀死这些线程
	int exitNum;

	//防止线程访问冲突，需要加锁
	pthread_mutex_t mutexPool;	//线程池的锁
	//方便修改线程数量
	pthread_mutex_t mutexBusy;


	//销毁线程
	int shutDown;	//销毁线程池为1，不销毁为0

	//使用条件变量，判定线程池状态
	pthread_cond_t notFull;		//判断是否满
	pthread_cond_t notEmpty;	//判断是否空
};



ThreadPool* threadPoolCreate(int min, int max, int queueSize)
{
    ThreadPool* pool=(ThreadPool*)malloc(sizeof(ThreadPool));

    //可以使用break 替代 return，在函数内部退出
    do
    {
        if(pool==NULL)
        {
            //内存分配失败
            printf("malloc threadpool fail...\n");
            //return NULL;
            break;
        }
        
        //初始化结构体成员
        pool->threadIDs=(pthread_t*)malloc(sizeof(pthread_t)*max);
        if(pool->threadIDs==NULL)
        {
            //内存分配失败
            printf("malloc pthreadIDs fail...\n");
            //return NULL;
            break;
        }
        
        //初始化线程
        memset(pool->threadIDs,0,sizeof(pthread_t)*max);
        pool->minNum=min;
        pool->maxNum=max;
        pool->busyNum=0;
        pool->liveNum=min;
        pool->exitNum=0;

        //初始化线程
        if(pthread_mutex_init(&pool->mutexPool,NULL)!=0 ||
            pthread_mutex_init(&pool->mutexBusy,NULL) !=0||
            pthread_cond_init(&pool->notEmpty,NULL)!=0 ||
            pthread_cond_init(&pool->notFull,NULL)!=0)
        {
            //初始化线程失败
            printf("mutex or condition init fail...\n");

            //return NULL;
            break;
        }

        //创建任务
        pool->taskQ=(Task*)malloc(sizeof(Task)*queueSize);
        pool->queueCapacity=queueSize;
        pool->queueSize=0;
        pool->queueFront=0;
        pool->queueTail=0;

        //初始时不销毁
        pool->shutDown=0;

        //创建线程
        pthread_create(&pool->managerID,NULL,manager,pool); //管理者线程
        for(int i=0;i<min;i++)
        {
            pthread_create(&pool->threadIDs[i],NULL,worker,pool);   //工作者线程
        }

        //申请空间成功，返回申请的线程池
        return pool;

    } while (0);    //如果出现异常，就会推出此循环，进行下面资源的释放
    
    //资源释放
    if(pool&&pool->threadIDs)
    {
        free(pool->threadIDs);
    }
    if(pool&&pool->taskQ)
    {
        free(pool->taskQ);
    }

    if(pool)
    {
        free(pool);
    }
    
    //申请空间失败，返回空
	return NULL; 
}

//当前线程池中线程个数
int threadPoolBusyNum(ThreadPool* pool)
{
    pthread_mutex_lock(&pool->mutexBusy);
    int busyNum=pool->busyNum;
    pthread_mutex_unlock(&pool->mutexBusy);

    return busyNum;
}

//获取当前创建的线程个数
int threadPoolAliveNum(ThreadPool* pool)
{
    pthread_mutex_lock(&pool->mutexPool);
    int aliveNum=pool->liveNum;
    pthread_mutex_unlock(&pool->mutexPool);

    return aliveNum;
}

//工作函数
void * worker(void* arg)
{
    //类型转换
    ThreadPool* pool=(ThreadPool*) arg;

    while(1)
    {
        //线程使用之前加锁
        pthread_mutex_lock(&pool->mutexPool);

        //判断当前任务队列
        while (pool->queueSize==0&&!pool->shutDown)
        {
            //阻塞工作线程
            pthread_cond_wait(&pool->notEmpty,&pool->mutexPool);

            //销毁 阻塞的工作线程
            if(pool->exitNum>0)
            {
                pool->exitNum--;
                if (pool->liveNum>pool->minNum)
                {
                    //减少线程数
                    pool->liveNum--;
                    //解开互斥锁
                    pthread_mutex_unlock(&pool->mutexPool);
                    //销毁线程
                    //pthread_exit(NULL);
                    threadExit(pool);
                }
     
            }
        }

        //判断线程池是否被关闭了
        if(pool->shutDown)
        {
            //先解锁, 避免死锁
            pthread_mutex_unlock(&pool->mutexPool);
            //退出线程
            //pthread_exit(NULL);
            threadExit(pool);
        }
        
        //从任务队列中取出一个任务
        Task task;
        task.function=pool->taskQ[pool->queueFront].function;   //任务
        task.arg=pool->taskQ[pool->queueFront].arg;

        //移动 queueFront，方便复用
        pool->queueFront=(pool->queueFront+1)%pool->queueCapacity;  //类似循环队列的做法
        //取出任务后，将任务队列中的总个数减少
        pool->queueSize--;

        //线程使用完之后解锁
        pthread_cond_signal(&pool->notFull);   //唤醒生产者
        pthread_mutex_unlock(&pool->mutexPool);
        

        printf("thread %ld start working...\n",pthread_self());
        //该线程在工作时，有可能其他线程也在工作，所以 忙线程也是共享资源，需要 锁
        pthread_mutex_lock(&pool->mutexBusy); //忙线程 加锁
        pool->busyNum++;
        pthread_mutex_unlock(&pool->mutexBusy);   //解锁

        //取出任务, 处理任务
        task.function(task.arg);

        //任务处理结束后，要释放掉 堆内存, 即传入的num
        free(task.arg);
        task.arg=NULL;

        printf("thread %ld end working...\n",pthread_self());
        //任务处理结束后，需要将 忙线程数再减少
        pthread_mutex_lock(&pool->mutexBusy); //忙线程 加锁
        pool->busyNum--;
        pthread_mutex_unlock(&pool->mutexBusy);   //解锁
    }
    return NULL;
}

//管理者线程
void * manager(void* arg)
{
    ThreadPool* pool=(ThreadPool*)arg;
    //安装一定的频率，检测 后 调整线程个数
    while(!pool->shutDown)  //线程池未关闭就检测
    {
        //每隔3S 检测一次
        sleep(3);

        //取出线程池中任务的数量和当前线程池的数量, 防止有其他线程在写入数据，所以需要 锁
        pthread_mutex_lock(&pool->mutexPool);
        int queueSize=pool->queueSize;
        int liveNum=pool->liveNum;
        pthread_mutex_unlock(&pool->mutexPool);

        //取出忙线程数量
        pthread_mutex_lock(&pool->mutexBusy);
        int busyNum=pool->busyNum;  //这里出于效率考虑，不在上面一起 获取，而是单独设置锁，单独获取
        pthread_mutex_unlock(&pool->mutexBusy);

        //添加线程
        //任务个数>存活的线程个数 并且 存活的线程个数<最大线程数 时，才会继续增加新线程
        if(queueSize>liveNum&&liveNum<pool->maxNum)
        {
            pthread_mutex_lock(&pool->mutexPool);
            int counter=0;
            //不仅要在上面判断，也需要在 这里再次判断，防止在这期间数量发送变化，导致 个数出现问题
            for(int i=0; i < pool->maxNum && counter < NUMBER && pool->liveNum < pool->maxNum;i++)
            {
                //找到未使用的线程ID(被销毁的线程)
                if(pool->threadIDs[i]==0)
                {
                    //创建线程, 直接使用该线程ID
                    pthread_create(&pool->threadIDs[i],NULL,worker,pool);
                    counter++;  //总线程个数
                    pool->liveNum++;    //存活线程个数
                }
            }
            pthread_mutex_unlock(&pool->mutexPool);
        }

        //销毁线程
        //销毁线程的条件：忙线程*2<存活线程 并且 存活的线程>最小线程数
        if(busyNum*2<liveNum && liveNum>pool->minNum)
        {
            //操作线程池中的值都要加锁
            pthread_mutex_lock(&pool->mutexPool);
            //每次销毁后值保留两个线程
            pool->exitNum=NUMBER;
            pthread_mutex_unlock(&pool->mutexPool);

            //让线程自己结束
            for(int i=0;i<NUMBER;i++)
            {
                //唤醒 被阻塞的线程
                pthread_cond_signal(&pool->notEmpty);
            }

        }
    }

    return NULL;
}

void threadExit(ThreadPool* pool)
{
    //获取当前的线程ID
    pthread_t tid=pthread_self();

    for(int i=0;i<pool->maxNum;i++)
    {
        if(pool->threadIDs[i]==tid)
        {
            //tid这个线程需要退出, 将该线程的 ID修改为0
            pool->threadIDs[i]=0;
            printf("threadExit() called, %ld exiting...\n",tid);

            break;
        }
    }

    pthread_exit(NULL);
}

//销毁线程池
int threadPoolDestroy(ThreadPool* pool)
{
    if(pool==NULL)
    {
        return -1;
    }

    //关闭线程池
    pool->shutDown=1;

    //回收阻塞管理者线程
    pthread_join(pool->managerID,NULL);
    //唤醒阻塞的消费者线程
    for(int i=0;i<pool->liveNum;i++)
    {
        pthread_cond_signal(&pool->notEmpty);
    }

    //释放堆内存
    if(pool->taskQ)
    {
        free(pool->taskQ);
    }
    if(pool->threadIDs)
    {
        free(pool->threadIDs);
    }
    
    //销毁互斥锁
    pthread_mutex_destroy(&pool->mutexPool);
    pthread_mutex_destroy(&pool->mutexBusy);
    pthread_cond_destroy(&pool->notEmpty);
    pthread_cond_destroy(&pool->notFull);
    
    //先销毁锁，再释放资源
    free(pool);
    pool=NULL;

    return 0;
}

//向线程池中添加任务
void threadPoolAdd(ThreadPool* pool, void(*func)(void*),void* arg)
{
    //防止对互斥资源的同步访问，需要 锁
    pthread_mutex_lock(&pool->mutexPool);
    while(pool->queueSize==pool->queueCapacity&& !pool->shutDown)
    {
        //阻塞生产者线程
        pthread_cond_wait(&pool->notFull,&pool->mutexPool);
    }

    if(pool->shutDown)
    {
        //如果线程池已经被关闭了，就 解锁并退出
        pthread_mutex_unlock(&pool->mutexPool);
        return;
    }

    //添加任务 到 队尾
    pool->taskQ[pool->queueTail].function=func;
    pool->taskQ[pool->queueTail].arg=arg;
    //扩大队尾
    pool->queueTail=(pool->queueTail+1)%pool->queueCapacity;
    pool->queueSize++;

    //唤醒阻塞的线程  
    pthread_cond_signal(&pool->notEmpty);   //唤醒消费者

    pthread_mutex_unlock(&pool->mutexPool);
}



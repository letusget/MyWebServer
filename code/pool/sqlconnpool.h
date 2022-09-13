#ifndef SQLCONNPOOL_H
#define SQLCONNPOOL_H

#include <mysql/mysql.h>
#include <string>
#include <queue>
#include <mutex>
#include <semaphore.h>
#include <thread>
#include "../log/log.h"

class SqlConnPool
{
public:
    //单例模式
    static SqlConnPool *Instance();

    //获取一个用户连接
    MYSQL *GetConn();
    //释放一个用户连接，放入 MySQL连接池
    void FreeConn(MYSQL *conn);
    //获取空闲用户个数
    int GetFreeConnCount();

    void Init(const char *host, int port,
              const char *user, const char *pwd,
              const char *dbName, int connSize);

    //关闭数据库连接池
    void ClosePool();

private:
    SqlConnPool();
    ~SqlConnPool();

    int MAX_CONN_;  // 最大的连接数
    int useCount_;  // 当前的用户数
    int freeCount_; // 空闲的用户数

    std::queue<MYSQL *> connQue_; // 队列（MYSQL *），操作数据库
    std::mutex mtx_;              // 互斥锁
    sem_t semId_;                 // 信号量
};

#endif // SQLCONNPOOL_H
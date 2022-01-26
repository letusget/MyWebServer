#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <sys/epoll.h>
#include <stdio.h>
#include <iostream>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include "thread_pool/locker.h"
#include <sys/uio.h>

class http_conn
{
private:
    //该 HTTP 连接的socket
    int m_sockfd;

    //连接信息, 通信的socket信息
    sockaddr_in m_address;

public:
    http_conn(/* args */);

    //处理客户端请求和响应
    void process();

    //所有的socket 上的事件，都要被注册到同一个epoll 对象上
    static int m_epollfd;
    //统计所有用户的数量
    static int m_user_count;

    //初始化新接收的连接
    void init(int sockfd, const sockaddr_in & addr);

    //处理 关闭连接
    void close_conn();

    //非阻塞 一次性读入数据
    bool read();

    //非堵塞，一次性写入数据
    bool write();
    ~http_conn();
};


#endif
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
    /* data */
public:
    http_conn(/* args */);

    //处理客户端请求和响应
    void process();

    ~http_conn();
};


#endif
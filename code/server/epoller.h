#ifndef EPOLLER_H
#define EPOLLER_H

#include <sys/epoll.h> //epoll_ctl()
#include <fcntl.h>  // fcntl()
#include <unistd.h> // close()
#include <assert.h> // close()
#include <vector>
#include <errno.h>

class Epoller {
public:
    //最大检测事件数，模式1024
    explicit Epoller(int maxEvent = 1024);

    ~Epoller();

    //添加检测事件，进行管理
    bool AddFd(int fd, uint32_t events);

    //修改连接事件
    bool ModFd(int fd, uint32_t events);

    //删除连接事件
    bool DelFd(int fd);

    //调用内核检测
    int Wait(int timeoutMs = -1);

    //获取连接事件的文件描述符
    int GetEventFd(size_t i) const;

    //获取连接事件
    uint32_t GetEvents(size_t i) const;
        
private:
    int epollFd_;   // epoll_create()创建一个epoll对象，返回值就是epollFd

    std::vector<struct epoll_event> events_;     // 检测到的事件(epoll连接事件)的集合 
};

#endif //EPOLLER_H
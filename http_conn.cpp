#include "http_conn.h"

http_conn::http_conn()
{
}

http_conn::~http_conn()
{
}

//向epoll 中添加需要的文件描述符
void addfd(int epollfd,int fd,bool one_shot)
{
    epoll_event event;
    event.data.fd=fd;
    //水平触发 2.6.17内核后支持EPOLLREHUP，可以通过事件直接判断，在底层对 连接 断开等进行处理
    event.events=EPOLLIN | EPOLLRDHUP;

    if(one_shot)
    {
        event.events |EPOLLONESHOT;
    }
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
}

//从 epoll 中移除监听的文件描述符
int removefd(int epollfd,int fd)
{
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
    close(fd);

}

//修改文件描述符, 要记得重置 socket 上的 EPOLLONSHOT 事件，确保下一次可读时，EPOLLIN 事件能被触发
void modfd(int epollfd,int fd,int ev)
{
    epoll_event event;
    event.data.fd=fd;
    event.events=ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}
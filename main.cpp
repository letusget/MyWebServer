#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string.h> //C语言
#include <libgen.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <signal.h>   //信号相关的头文件
#include <sys/epoll.h>
#include "thread_pool/threadpool.h"
#include "http_conn.h"

//最大客户端 文件描述符 个数
#define MAX_FD 65535
//最大监听的事件数
#define MAX_EVENT_NUMBER 10000


//信号捕捉函数
void addsig(int sig,void (handler)(int));

//添加文件描述符到 epoll 中
extern void addfd(int epollfd,int fd,bool one_shot);

// 从 epoll 中删除文件描述符
extern int removefd(int epollfd,int fd);

//修改文件描述符
extern void modfd(int epollfd,int fd,int ev);

//因为在运行时要指定端口号，所以在这里需要参数
int main(int argc, char* argv[])
{
    if( argc <= 1)
    {
        printf("请按照如下格式运行程序: %s port_number\n",basename(argv[0]));
        exit(-1);
    }

    //获取端口号, 要将输入的字符串的端口号转化为整形
    int port = atoi(argv[1]);

    //对 SIGPIE 信号进行处理: 忽略该信号
    addsig(SIGPIPE,SIG_IGN);

    //创建线程池，初始化信息
    threadpool<http_conn> * pool = nullptr;
    try
    {
        pool=new threadpool<http_conn>;
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        exit(-1);
    }

    //使用一个数组，保存所有用户任务的客户端信息
    http_conn * users=new http_conn[MAX_FD];

    //客户端连接：创建监听套接字
    int listenfd=socket(PF_INET,SOCK_STREAM,0);
    if(listenfd==-1)
    {
        perror("socket");
        exit(-1);
    }

    //设置端口复用
    int reuse=1;
    setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));

    //绑定
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr=INADDR_ANY;
    address.sin_port = htons(port); //转化为网络字节序
    int bret = bind(listenfd,(struct sockaddr* )&address,sizeof(address));
    if(bret==-1)
    {
        perror("bind");
        exit(-1);
    }

    
    //监听
    listen(listenfd,5);

    //创建epoll 对象，时间数组，添加文件描述符
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd=epoll_create(5);

    //将监听的文件描述符添加到 epoll 对象中
    addfd(epollfd,listenfd,false);

    

    return 0;
}

//信号捕捉函数
void addsig(int sig,void (handler)(int))
{
    //注册信号
    struct sigaction sa;
    //使用复制函数，提前清空sa内容
    memset(&sa,'\n',sizeof(sa));

    sa.sa_handler=handler;
    //临时阻塞信号值
    sigfillset(&sa.sa_mask);
    sigaction(sig,&sa,nullptr);

}
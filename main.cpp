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
#include <assert.h> //断言
#include "http_conn.h"

#include "threadpool.h"


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

//修改epoll 中的文件描述符
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

    //对 SIGPIE 信号进行处理: 忽略该信号，不终止进程
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

    // int ret=0;

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
    bret = listen(listenfd,5);

    //多路复用 创建epoll 对象，事件数组，添加文件描述符
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd=epoll_create(5);

    //将监听的文件描述符添加到 epoll 对象中
    addfd(epollfd,listenfd,false);
    http_conn::m_epollfd=epollfd;   //访问静态成员

    while (true)
    {
        int num = epoll_wait(epollfd,events,MAX_EVENT_NUMBER,-1);
        //调用失败的情况，直接退出循环
        if( ( num < 0 ) && (errno != EINTR ) )
        {
            printf("epoll failure\n");
            break;
        }
        
        //循环遍历 事件数组
        for (int i = 0; i < num; i++)
        {
            int sockfd=events[i].data.fd;
            if(sockfd==listenfd)
            {
                //有客户端连接
                struct sockaddr_in client_address;
                socklen_t client_addrlen=sizeof(client_address);
                int connfd = accept(listenfd,(struct sockaddr * )&client_address,&client_addrlen);
                if ( connfd < 0 ) 
                {
                    //连接失败的情况
                    printf( "errno is: %d\n", errno );
                    continue;
                } 

                if(http_conn::m_user_count>=MAX_FD)
                {
                    //目前连接数已满，可以给出提示
                    close(connfd);
                    continue;
                }

                //将新客户的数据 初始化，放在数组中,将文件描述符作为索引
                users[connfd].init(connfd, client_address);

            }
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR )) //异常通信
            {
                //对方异常断开，或是 错误 等事件
                users[sockfd].close_conn();
            }
            else if( events[i].events & EPOLLIN )
            {
                //一次性读入所有数据
                if( users[sockfd].read())
                {
                    //读入所有数据，进行处理
                    pool->append(users + sockfd );
                }
                else
                {
                    //读入失败，或没有读入信息 就关闭连接
                    users[sockfd].close_conn();
                }
            }
            else if(events[i].events & EPOLLOUT)
            {
                //一次性写入所有数据
                if(!users[sockfd].write())
                {
                    //如果没有全部写入，就关闭连接
                    users[sockfd].close_conn();
                }

            }
            
        }
        
        
    }
    
    //释放资源
    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;

    return 0;
}

//回调函数  信号捕捉函数  
void addsig(int sig,void (handler)(int))
{
    //注册信号
    struct sigaction sa;
    //注册信号的参数  使用复制函数，提前清空sa内容
    memset(&sa,'\n',sizeof(sa));

    sa.sa_handler=handler;
    //临时阻塞信号值
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig,&sa,nullptr)!=-1);
    
}
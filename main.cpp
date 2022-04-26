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
#include "lst_timer.h"  
#include "threadpool.h"


//最大客户端 文件描述符 个数
#define MAX_FD 65535
//最大监听的事件数
#define MAX_EVENT_NUMBER 10000
//活跃检测时间
#define TIMESLOT 600
static int pipefd[2];
static sort_timer_lst timer_lst;
static int epollfd = 0;


//信号捕捉函数
void addsig(int sig,void (handler)(int));
//超时连接 alarm信号
void addsig( int sig );

//添加文件描述符到 epoll 中
extern void addfd(int epollfd,int fd,bool one_shot);
//超时连接 添加到epoll 中
void addfd( int epollfd, int fd );

// 从 epoll 中删除文件描述符
extern int removefd(int epollfd,int fd);

//修改epoll 中的文件描述符
extern void modfd(int epollfd,int fd,int ev);

//
void sig_handler( int sig );
//
void timer_handler();
//
void cb_func( client_data* user_data );
//
int setnonblocking( int fd );

//因为在运行时要指定端口号，所以在这里需要接收参数
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
    //
    

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

    //检测连接的事件管理
    addfd( epollfd, listenfd );
    // 创建管道
    int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert( ret != -1 );
    setnonblocking( pipefd[1] );
    addfd( epollfd, pipefd[0] );

    // 设置信号处理函数
    addsig( SIGALRM );
    addsig( SIGTERM );
    bool stop_server = false;

    //测试用户连接
    client_data* A_users = new client_data[MAX_FD];
    bool timeout = false;
    alarm(TIMESLOT);  // 定时,5秒后产生SIGALARM信号

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

                //测试客户端连接
                addfd( epollfd, connfd );
                A_users[connfd].address = client_address;
                A_users[connfd].sockfd = connfd;
                
                // 创建定时器，设置其回调函数与超时时间，然后绑定定时器与用户数据，最后将定时器添加到链表timer_lst中
                util_timer* timer = new util_timer;
                timer->user_data = &A_users[connfd];
                timer->cb_func = cb_func;
                time_t cur = time( NULL );
                timer->expire = cur + 3 * TIMESLOT;
                A_users[connfd].timer = timer;
                timer_lst.add_timer( timer );

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

                //活跃用户连接
                memset( A_users[sockfd].buf, '\0', BUFFER_SIZE );
                ret = recv( sockfd, A_users[sockfd].buf, BUFFER_SIZE-1, 0 );
                printf( "get %d bytes of client data %s from %d\n", ret, A_users[sockfd].buf, sockfd );
                util_timer* timer = A_users[sockfd].timer;
                if( ret < 0 )
                {
                    // 如果发生读错误，则关闭连接，并移除其对应的定时器
                    if( errno != EAGAIN )
                    {
                        cb_func( &A_users[sockfd] );
                        if( timer )
                        {
                            timer_lst.del_timer( timer );
                        }
                    }
                }
                else if( ret == 0 )
                {
                    // 如果对方已经关闭连接，则我们也关闭连接，并移除对应的定时器。
                    cb_func( &A_users[sockfd] );
                    if( timer )
                    {
                        timer_lst.del_timer( timer );
                    }
                }
                else
                {
                    // 如果某个客户端上有数据可读，则我们要调整该连接对应的定时器，以延迟该连接被关闭的时间。
                    if( timer ) 
                    {
                        time_t cur = time( NULL );
                        timer->expire = cur + 3 * TIMESLOT;
                        printf( "adjust timer once\n" );
                        timer_lst.adjust_timer( timer );
                    }
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
            else if( ( sockfd == pipefd[0] ) && ( events[i].events & EPOLLIN ) ) 
            {
                // 处理信号
                int sig;
                char signals[1024];
                ret = recv( pipefd[0], signals, sizeof( signals ), 0 );
                if( ret == -1 ) 
                {
                    continue;
                } 
                else if( ret == 0 ) 
                {
                    continue;
                } 
                else  
                {
                    for( int i = 0; i < ret; ++i ) 
                    {
                        switch( signals[i] )  
                        {
                            case SIGALRM:
                            {
                                // 用timeout变量标记有定时任务需要处理，但不立即处理定时任务
                                // 这是因为定时任务的优先级不是很高，我们优先处理其他更重要的任务。
                                timeout = true;
                                break;
                            }
                            case SIGTERM:
                            {
                                stop_server = true;
                            }
                        }
                    }
                }
            }
            
        }
        
        // 最后处理定时事件，因为I/O事件有更高的优先级。当然，这样做将导致定时任务不能精准的按照预定的时间执行。
        if( timeout ) 
        {
            timer_handler();
            timeout = false;
        } 
    }
    
    //释放资源
    close(epollfd);
    close(listenfd);
    //客户端活跃连接
    close( pipefd[1] );
    close( pipefd[0] );

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

//客户端活跃连接
int setnonblocking( int fd )
{
    int old_option = fcntl( fd, F_GETFL );
    int new_option = old_option | O_NONBLOCK;
    fcntl( fd, F_SETFL, new_option );
    return old_option;
}

void addfd( int epollfd, int fd )
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl( epollfd, EPOLL_CTL_ADD, fd, &event );
    setnonblocking( fd );
}

void sig_handler( int sig )
{
    int save_errno = errno;
    int msg = sig;
    send( pipefd[1], ( char* )&msg, 1, 0 );
    errno = save_errno;
}

void addsig( int sig )
{
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = sig_handler;
    sa.sa_flags |= SA_RESTART;
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}

void timer_handler()
{
    // 定时处理任务，实际上就是调用tick()函数
    timer_lst.tick();
    // 因为一次 alarm 调用只会引起一次SIGALARM 信号，所以我们要重新定时，以不断触发 SIGALARM信号。
    alarm(TIMESLOT);    
}

// 定时器回调函数，它删除非活动连接socket上的注册事件，并关闭之。
void cb_func( client_data* user_data )
{
    epoll_ctl( epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0 );
    assert( user_data );
    close( user_data->sockfd );
    printf( "close fd %d\n", user_data->sockfd );
    
}
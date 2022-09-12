#include "webserver.h"

using namespace std;

WebServer::WebServer(
    int port, int trigMode, int timeoutMS, bool OptLinger,
    int sqlPort, const char *sqlUser, const char *sqlPwd,
    const char *dbName, int connPoolNum, int threadNum,
    bool openLog, int logLevel, int logQueSize) : port_(port), openLinger_(OptLinger), timeoutMS_(timeoutMS), isClose_(false),
                                                  timer_(new HeapTimer()), threadpool_(new ThreadPool(threadNum)), epoller_(new Epoller())
{
    // /root/webserver
    srcDir_ = getcwd(nullptr, 256); // 获取当前的工作路径, 在执行目录确定工作路径
    assert(srcDir_);

    // /root/webserverresources/
    strncat(srcDir_, "/resources/", 16); // 拼接资源路径，访问资源

    // 当前所有连接数
    HttpConn::userCount = 0;
    HttpConn::srcDir = srcDir_;

    // 初始化数据库连接池
    SqlConnPool::Instance()->Init("localhost", sqlPort, sqlUser, sqlPwd, dbName, connPoolNum);

    // 初始化事件的模式
    InitEventMode_(trigMode);

    // 初始化网络通信相关的一些内容
    if (!InitSocket_())
    {
        isClose_ = true;
    }

    if (openLog)
    {
        // 初始化日志信息
        Log::Instance()->init(logLevel, "./log", ".log", logQueSize);
        if (isClose_)
        {
            LOG_ERROR("========== Server init error!==========");
        }
        else
        {
            LOG_INFO("========== Server init ==========");
            LOG_INFO("Port:%d, OpenLinger: %s", port_, OptLinger ? "true" : "false");
            LOG_INFO("Listen Mode: %s, OpenConn Mode: %s",
                     (listenEvent_ & EPOLLET ? "ET" : "LT"),
                     (connEvent_ & EPOLLET ? "ET" : "LT"));
            LOG_INFO("LogSys level: %d", logLevel);
            LOG_INFO("srcDir: %s", HttpConn::srcDir);
            LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d", connPoolNum, threadNum);
        }
    }
}

WebServer::~WebServer()
{
    close(listenFd_);
    isClose_ = true;
    free(srcDir_);
    SqlConnPool::Instance()->ClosePool();
}

// 设置监听的文件描述符和通信的文件描述符的模式
void WebServer::InitEventMode_(int trigMode)
{
    listenEvent_ = EPOLLRDHUP;              //检测文件描述符是否正常关闭
    connEvent_ = EPOLLONESHOT | EPOLLRDHUP; //
    switch (trigMode)
    {
    case 0:
        break;
    case 1:
        connEvent_ |= EPOLLET; // ET 模式通信
        break;
    case 2:
        listenEvent_ |= EPOLLET; // ET 模式监听
        break;
    case 3:
        // 均使用ET 模式
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET;
        break;
    default:
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET;
        break;
    }
    // 设置ET 模式
    HttpConn::isET = (connEvent_ & EPOLLET);
}

// 启动服务器
void WebServer::Start()
{
    int timeMS = -1; /* epoll wait timeout == -1 无事件将阻塞 */
    if (!isClose_)
    {
        LOG_INFO("========== Server start ==========");
    }

    //服务器未关闭就一直运行
    while (!isClose_)
    {
        // 如果设置了超时时间，例如60s,则只要一个连接60秒没有读写操作，则关闭
        if (timeoutMS_ > 0)
        {
            // 通过定时器GetNextTick(),清除超时的节点，然后获取最先要超时的连接的超时的时间
            timeMS = timer_->GetNextTick();
        }

        // timeMS是最先要超时的连接的超时的时间，传递到epoll_wait()函数中
        // 当timeMS时间内有事件发生，epoll_wait()返回，否则等到了timeMS时间后才返回
        // 这样做的目的是为了让epoll_wait()调用次数变少，提高效率
        int eventCnt = epoller_->Wait(timeMS);

        // 循环处理每一个事件
        for (int i = 0; i < eventCnt; i++)
        {
            /* 处理事件 */
            int fd = epoller_->GetEventFd(i);         // 获取事件对应的fd
            uint32_t events = epoller_->GetEvents(i); // 获取事件的类型

            // 监听的文件描述符有事件，说明有新的连接进来
            if (fd == listenFd_)
            {
                DealListen_(); // 处理监听的操作，接受客户端连接
            }
            // 错误的情况
            else if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                assert(users_.count(fd) > 0);
                CloseConn_(&users_[fd]); // 关闭连接
            }
            // 有数据到达, 读事件
            else if (events & EPOLLIN)
            {
                assert(users_.count(fd) > 0);
                DealRead_(&users_[fd]); // 处理读操作
            }
            // 可以发送数据
            else if (events & EPOLLOUT)
            {
                assert(users_.count(fd) > 0);
                DealWrite_(&users_[fd]); // 处理写操作
            }
            else
            {
                LOG_ERROR("Unexpected event");
            }
        }
    }
}

// 发送错误提示信息
void WebServer::SendError_(int fd, const char *info)
{
    assert(fd > 0);
    int ret = send(fd, info, strlen(info), 0);
    if (ret < 0)
    {
        LOG_WARN("send error to client[%d] error!", fd);
    }
    close(fd);
}

// 关闭连接（从epoll中删除，解除响应对象中的内存映射，用户数递减，关闭文件描述符）
void WebServer::CloseConn_(HttpConn *client)
{
    assert(client);
    LOG_INFO("Client[%d] quit!", client->GetFd());
    epoller_->DelFd(client->GetFd());
    //关闭连接
    client->Close();
}

// 添加客户端
void WebServer::AddClient_(int fd, sockaddr_in addr)
{
    assert(fd > 0);
    //获取连接的文件
    users_[fd].init(fd, addr);
    //超时处理
    if (timeoutMS_ > 0)
    {
        // 添加到定时器对象中，当检测到超时时执行CloseConn_函数进行关闭连接
        timer_->add(fd, timeoutMS_, std::bind(&WebServer::CloseConn_, this, &users_[fd]));
    }
    // 添加到epoll中进行管理
    epoller_->AddFd(fd, EPOLLIN | connEvent_);
    // 设置文件描述符非阻塞
    SetFdNonblock(fd);
    LOG_INFO("Client[%d] in!", users_[fd].GetFd());
}

//连接客户端
void WebServer::DealListen_()
{
    struct sockaddr_in addr; // 保存连接的客户端的信息
    socklen_t len = sizeof(addr);

    // 如果监听文件描述符设置的是 ET模式，则需要循环把所有连接处理了
    do
    {
        // 得到客户端连接的文件描述符
        int fd = accept(listenFd_, (struct sockaddr *)&addr, &len);
        if (fd <= 0)
        {
            return;
        }
        else if (HttpConn::userCount >= MAX_FD)
        {
            //超出最大 客户端连接数，对客户端发送数据
            SendError_(fd, "Server busy!");
            LOG_WARN("Clients is full!");
            return;
        }

        AddClient_(fd, addr); // 添加客户端
    } while (listenEvent_ & EPOLLET);
}

// 处理读
void WebServer::DealRead_(HttpConn *client)
{
    assert(client);
    ExtentTime_(client); // 延长这个客户端的超时时间

    // 加入到队列中等待线程池中的线程处理（读取数据）reactor模式
    threadpool_->AddTask(std::bind(&WebServer::OnRead_, this, client));
}

// 处理写
void WebServer::DealWrite_(HttpConn *client)
{
    assert(client);
    ExtentTime_(client); // 延长这个客户端的超时时间

    // 加入到队列中等待线程池中的线程处理（写数据）
    threadpool_->AddTask(std::bind(&WebServer::OnWrite_, this, client));
}

// 延长客户端的超时时间
void WebServer::ExtentTime_(HttpConn *client)
{
    assert(client);
    if (timeoutMS_ > 0)
    {
        timer_->adjust(client->GetFd(), timeoutMS_);
    }
}

// 这个方法是在子线程中执行的（读取数据）
void WebServer::OnRead_(HttpConn *client)
{
    assert(client);
    int ret = -1;
    int readErrno = 0;
    ret = client->read(&readErrno); // 读取客户端的数据，读取到readbuf中

    //判断返回值和错误号
    if (ret <= 0 && readErrno != EAGAIN)
    {
        CloseConn_(client);
        return;
    }

    // 业务逻辑的处理
    OnProcess(client);
}

// 业务逻辑的处理
void WebServer::OnProcess(HttpConn *client)
{
    //调用客户端 进行业务逻辑处理
    if (client->process())
    {
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
    }
    else
    {
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN);
    }
}

// 写数据
void WebServer::OnWrite_(HttpConn *client)
{
    assert(client);
    int ret = -1;
    int writeErrno = 0;
    ret = client->write(&writeErrno); // 写数据

    // 如果将要写的字节等于0，说明写完了，判断是否要保持连接，保持连接继续去处理
    if (client->ToWriteBytes() == 0)
    {
        /* 传输完成 */
        if (client->IsKeepAlive())
        {
            OnProcess(client);
            return;
        }
    }
    else if (ret < 0)
    {
        if (writeErrno == EAGAIN)
        {
            /* 继续传输 */
            epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
            return;
        }
    }
    CloseConn_(client);
}

/* Create listenFd */
bool WebServer::InitSocket_()
{
    int ret;
    struct sockaddr_in addr;
    //判断合法端口号，非常用端口号和客户端端口号
    if (port_ > 65535 || port_ < 1024)
    {
        LOG_ERROR("Port:%d error!", port_);
        return false;
    }
    addr.sin_family = AF_INET;
    //字节序转换
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);
    struct linger optLinger = {0};

    if (openLinger_)
    {
        /* 优雅关闭: 直到所剩数据发送完毕或超时 */
        optLinger.l_onoff = 1;
        optLinger.l_linger = 1;
    }

    //用于连接的socket
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0)
    {
        LOG_ERROR("Create socket error!", port_);
        return false;
    }

    ret = setsockopt(listenFd_, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger));
    if (ret < 0)
    {
        close(listenFd_);
        LOG_ERROR("Init linger error!", port_);
        return false;
    }

    int optval = 1;
    /* 端口复用 */
    /* 只有最后一个套接字会正常接收数据。 */
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int));
    if (ret == -1)
    {
        LOG_ERROR("set socket setsockopt error !");
        close(listenFd_);
        return false;
    }

    //绑定端口号
    ret = bind(listenFd_, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0)
    {
        LOG_ERROR("Bind Port:%d error!", port_);
        close(listenFd_);
        return false;
    }

    // 监听端口号
    ret = listen(listenFd_, 6);
    if (ret < 0)
    {
        LOG_ERROR("Listen port:%d error!", port_);
        close(listenFd_);
        return false;
    }

    //添加监听事件
    ret = epoller_->AddFd(listenFd_, listenEvent_ | EPOLLIN);
    if (ret == 0)
    {
        LOG_ERROR("Add listen error!");
        close(listenFd_);
        return false;
    }

    SetFdNonblock(listenFd_);
    LOG_INFO("Server port:%d", port_);
    return true;
}

// 设置文件描述符非阻塞
int WebServer::SetFdNonblock(int fd)
{
    assert(fd > 0);

    // int flag = fcntl(fd, F_GETFD, 0);    //获取原值
    // flag = flag  | O_NONBLOCK;   //设置非阻塞
    // // flag  |= O_NONBLOCK;
    // fcntl(fd, F_SETFL, flag);    //设置文件描述符非阻塞
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFD, 0) | O_NONBLOCK);
}

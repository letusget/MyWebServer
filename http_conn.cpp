#include "http_conn.h"

int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;


http_conn::http_conn()
{
}

http_conn::~http_conn()
{
}

//设置文件描述符 非阻塞
void setnoblocking(int fd)
{
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
}

//修改文件描述符, 要记得重置 socket 上的 EPOLLONSHOT 事件，确保下一次可读时，EPOLLIN 事件能被触发
void modfd(int epollfd,int fd,int ev)
{
    epoll_event event;
    event.data.fd=fd;
    event.events=ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}

//向epoll 中添加需要的文件描述符
void addfd(int epollfd,int fd,bool one_shot)
{
    epoll_event event;
    event.data.fd=fd;
    //水平触发 2.6.17内核后支持EPOLLREHUP，可以通过事件直接判断，在底层对 连接 断开等进行处理
    //event.events=EPOLLIN | EPOLLRDHUP;
    //边沿触发, 但是这样 listenfd 也变为 边沿触发了，稍微有点问题
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;

    if(one_shot)
    {
        event.events |EPOLLONESHOT;
    }
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);

    //设置文件描述符 非阻塞，否则就会一直阻塞等待用户连接
    setnoblocking(fd);
}

//从 epoll 中移除监听的文件描述符
void removefd(int epollfd,int fd)
{
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
    close(fd);

}

//处理客户端请求和响应
 //由线程池的工作线程调用，是处理 HTTP 请求的入口函数
void http_conn::process()
{
    //解析HTTP 请求
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST )
    {
        //请求不完整, 重新检测
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        return;
    }


    //生成响应


}

//初始化新接收的连接
void http_conn::init(int sockfd, const sockaddr_in & addr)
{
    m_sockfd = sockfd;
    m_address=addr;

    //设置端口复用
    int reuse=1;
    setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));

    //添加到 epoll 对象中
    addfd(m_epollfd, sockfd, true);
    m_user_count++; //总用户数增加
}

//处理 关闭连接
void http_conn::close_conn()
{
    //判断连接情况
    if(m_sockfd!=-1)
    {
        removefd(m_epollfd,m_sockfd);
        m_sockfd=-1;

        //关闭一个连接，用户数减少一个
        m_user_count--;
    }

}

//非阻塞 一次性读入数据，直到无数据可读或者对方关闭连接
bool http_conn::read()
{
    //缓冲区已满
    if(m_read_idx>=READ_BUFFER_SIZE)
    {
        return false;
    }

    //读取到的字节
    int bytes_read = 0;
    while (true)
    {
        bytes_read = recv(m_sockfd,m_read_buf+m_read_idx,READ_BUFFER_SIZE-m_read_idx,0);
        if(bytes_read == -1)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                //没有数据
                break;
            }
            return false;
        }
        else if(bytes_read == 0)
        {
            //对方关闭连接
            return false;
        }

        //读到数据了
        m_read_idx += bytes_read;
        
    }

    //读入到了数据
    printf("读取到了数据：%s\n",m_read_buf);
    return true;
    
    
}

//非堵塞，一次性写入数据
bool http_conn::write()
{
    //暂时这样处理
    printf("一次性 写入数据\n");
    return true;
}


//解析 HTTP 请求
http_conn::HTTP_CODE http_conn::process_read()
{

    return NO_REQUEST;
}

//解析请求首行
http_conn::HTTP_CODE http_conn::parse_request_line(char * text)
{

}

//解析请求头
http_conn::HTTP_CODE http_conn::parse_headers(char * text)
{

}

//解析请求体
http_conn::HTTP_CODE http_conn::parse_content(char * text)
{

}

//解析某一行数据 根据换行判断，再根据这行的状态，来交给上面对应的处理函数
http_conn::LINE_STATUS http_conn::parse_line()
{
    return LINE_OK;
}





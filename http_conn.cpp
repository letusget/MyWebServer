#include "http_conn.h"

int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;

const char* doc_root="/root/webserver/resources";

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

    //初始化其他信息,因为可能会用到其他的浙西信息，所以分开初始化会更方便
    init();
}

//初始化以上的（其他信息）
void http_conn::init()    //函数重载
{
    //初始化 状态为解析请求首行
    m_check_state = CHECK_STATE_REQUESTLINE;

    //当前状态
    m_checked_index = 0;

    //当前解析行
    m_start_line = 0;
    m_read_idx = 0;

    //请求行信息的初始化
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_linger = false;
    m_host = 0;

    //清空读缓冲区
    bzero(m_read_buf,READ_BUFFER_SIZE);


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
    printf("读取到了数据: %s\n",m_read_buf);
    return true;
    
    
}

//非堵塞，一次性写入数据
bool http_conn::write()
{
    //暂时这样处理
    printf("一次性 写入数据\n");
    return true;
}

// 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
// 映射到内存地址m_file_address处，并告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request()
{
    //  /root/webserver/resources
    strcpy(m_real_file,doc_root);

    //


}
//解析 HTTP 请求
http_conn::HTTP_CODE http_conn::process_read()
{
    //定义初始状态
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;

    //要获取的数据
    char * text = 0;

    //解析一行数据看是否正确，根据此行状态 再做处理
    //解析请求体且行检查完  或 获取到一行解析数据完成
    while (((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) 
        || (line_status = parse_line()) == LINE_OK)
    {
        //获取一行数据
        text = get_line();

        m_start_line = m_checked_index;
        printf("get 1 http line : %s\n",text);

        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
        {
            ret = parse_request_line(text);
            if(ret == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            break;
        }

        case CHECK_STATE_HEADER:
        {
            ret = parse_headers(text);
            if(ret == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            else if(ret == GET_REQUEST)
            {
                //具体解析请求
                return do_request();
            }

        }
        case CHECK_STATE_CONTENT:
        {
            ret = parse_content(text);
            if(ret == GET_REQUEST)
            {
                return do_request();
            }
            line_status = LINE_OPEN;
            break;
        }
            
        
        default:
        {
            return INTERNAL_ERROR;
            break;
        }
            
        }
    } 

    return NO_REQUEST;
}

//解析请求首行：获取请求方法，目标URL，HTTP版本
http_conn::HTTP_CODE http_conn::parse_request_line(char * text)
{
    //使用正则表达式 进行处理 或 传统字符串操作匹配
    // GET /index.html HTTP/1.1
    m_url = strpbrk (text, " \t");   //检验 \n 的位置

    //变为 GET\0/index.html HTTP/1.1
    *m_url ++ = '\0';

    //得到请求方法：GET
    char * method = text;
    if(strcasecmp(method,"GET")==0)
    {
        m_method = GET;
    }
    else
    {
        //目前仅支持 GET 请求方法
        return BAD_REQUEST;
    }

    //获取协议版本
    //在去掉请求方法之后：/index.html HTTP/1.1
    m_version = strpbrk(m_url," \t");
    if(!m_version)
    {
        //默认仅支持 HTTP1.1
        return BAD_REQUEST;
    }
    *m_version ++ = '\0';
    //变为：/index.html\0HTTP/1.1
    if(strcasecmp(m_version,"HTTP/1.1")!=0)
    {
        return BAD_REQUEST;
    }

    //  /index.html  或是 http://192.168.1.1:9999/index.html
    if(strncasecmp(m_url,"http://",7)==0)   //判断前7个字符是否为要求字符
    {
        m_url+=7;
        //寻找 / 出现的索引
        m_url = strchr(m_url,'/');

    }
    if(!m_url || m_url[0] != '/')
    {
        return BAD_REQUEST;
    }

    //检测完首行，就改变主状态机的状态, 变为检查请求头
    m_check_state = CHECK_STATE_HEADER;

    return NO_REQUEST;
}

//解析请求头
http_conn::HTTP_CODE http_conn::parse_headers(char * text)
{
    //遇到空行，就表示头部字段已经解析完毕了
    if(text[0] == '\0')
    {
        //如果 HTTP 请求有消息体，则还需要读取 m_conten_length字节的消息体
        //状态机转移到 CHECK_STATE_CONTENT 状态
        if(m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        //否则就说明已经得到了一个完整的HTTP请求 
        return GET_REQUEST;
    }
    else if(strncasecmp( text,"Connection:",11)==0) //请求头
    {
        //处理 Connection 头部字段 Connection: keep-alive
        text+=11;
        text += strspn(text," \t");
        if( strcasecmp(text,"keep-alive") == 0)
        {
            //保持连接
            m_linger = true;
        }
    }
    else if(strncasecmp(text,"Conntent-Length:",15)==0)
    {
        //处理 Content-Length 头部
        text += 15;
        text += strspn(text," \t");
        m_content_length = atol(text);
    }
    else if(strncasecmp(text,"Host:",5)==0)
    {
        //处理Host头部字段
        text+=5;
        text+=strspn(text," \n");
        m_host = text;
    }
    else 
    {
        printf("Oh No! unknow header %s\n",text);
    }
    return NO_REQUEST;

}

//解析请求体, 这里没有真正解析请求体，只是判断它是否被读入了
http_conn::HTTP_CODE http_conn::parse_content(char * text)
{
    if(m_read_idx >= (m_content_length + m_checked_index))
    {
        text [ m_content_length ] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

//解析某一行数据 根据换行判断，再根据这行的状态，来交给上面对应的处理函数
//根据 /r  /n 进行判断
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for(; m_checked_index<m_read_idx;++m_checked_index)
    {
        //当前位置
        temp=m_read_buf[m_checked_index];

        if(temp == '\r')
        {
            //当前读取位置的下一个为已经读到的位置
            if((m_checked_index + 1)==m_read_idx)
            {
                return LINE_OPEN;
            }
            else if(m_read_buf[m_checked_index +1 ]=='\n')
            {
                //将原来的 \n 换为 \0 (字符串结束符)
                m_read_buf[m_checked_index++]='\0';
                //将原来的 \t 换为 \0 (字符串结束符)
                m_read_buf[m_checked_index++]='\n';
                return LINE_OK;
            }
            return LINE_BAD;

        }
        //本来末尾是 \r 和 \n， 这里读到 \n 就相当于读到了第二个位置
        else if(temp=='\n')
        {
            if((m_checked_index > 1) && (m_read_buf[m_checked_index-1]=='\r'))
            {
                //放入数组结束符
                m_read_buf[m_checked_index-1] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }

        //数据不完整
        return LINE_OPEN;

    }

    return LINE_OK;
}





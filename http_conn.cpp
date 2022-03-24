#include "http_conn.h"

//类外初始化
// 所有socket上的事件都被注册到同一个epoll内核事件中，所以设置成静态的
int http_conn::m_epollfd = -1;
// 所有的客户数
int http_conn::m_user_count = 0;

//网站的根目录
const char* doc_root="/root/webserver/resources";


//定义HTTP 响应的一些状态信息，响应描述信息
const char * ok_200_title = "OK";
const char * error_400_title = "Bad Request";
const char * error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char * error_403_title = "Forbidden";
const char * error_403_form = "You do not have permission to get file from this server.\n";
const char * error_404_title = "Not Found";
const char * error_404_form = "The requested file was not found on this server.\n";
const char * error_500_title = "Internal Error";
const char * error_500_form = "There was an unusual problem serving the requested file.\n";

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
    //return old_flag;    //TODO
}

//向epoll 中添加需要的文件描述符
void addfd(int epollfd,int fd,bool one_shot)
{
    epoll_event event;
    event.data.fd=fd;
    //水平触发 2.6.17内核后支持EPOLLREHUP，可以通过事件直接判断，在底层对 连接 断开等进行处理
    //event.events=EPOLLIN | EPOLLRDHUP;
    //边沿触发, 但是这样 listenfd 也变为 边沿触发了，稍微有点问题
    //event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    event.events = EPOLLIN | EPOLLRDHUP;    //水平触发

    if(one_shot)
    {
        event.events |= EPOLLONESHOT;   //TODO
    }
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);

    //设置文件描述符 非阻塞，否则就会一直阻塞等待用户连接
    setnoblocking(fd);
}

//从 epoll 中移除监听的文件描述符
void removefd(int epollfd,int fd)
{
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
    //关闭文件描述符
    close(fd);

}

//修改文件描述符, 要记得重置 socket 上的 EPOLLONESHOT 事件，确保下一次可读时，EPOLLIN 事件能被触发
void modfd(int epollfd,int fd,int ev)
{
    epoll_event event;
    event.data.fd=fd;
    //event.events=ev | EPOLLONESHOT | EPOLLRDHUP;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}

//处理 关闭连接
void http_conn::close_conn()
{
    //判断连接情况
    if(m_sockfd!=-1)
    {
        //关闭连接
        removefd(m_epollfd,m_sockfd);
        m_sockfd=-1;

        //关闭一个连接，用户数减少一个
        m_user_count--;
    }

}


//初始化新接收的连接
void http_conn::init(int sockfd, const sockaddr_in & addr)
{
    //初始化
    m_sockfd = sockfd;
    m_address=addr;

    //设置端口复用
    int reuse=1;
    setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));

    //添加到 epoll 对象中
    addfd(m_epollfd, sockfd, true);
    m_user_count++; //总用户数增加

    //初始化其他信息,因为可能会用到其他的这些信息，所以分开初始化会更方便
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
    m_linger = false;   // 默认不保持链接  Connection : keep-alive保持连接
    m_host = 0;
    m_start_line = 0;

    m_checked_index = 0;

    m_read_idx = 0;
    m_write_idx = 0;

    //清空读缓冲区
    bzero(m_read_buf,READ_BUFFER_SIZE);
    bzero(m_write_buf,READ_BUFFER_SIZE);
    bzero(m_real_file,FILENAME_LEN);

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
        // 从m_read_buf + m_read_idx索引出开始保存数据，大小是READ_BUFFER_SIZE - m_read_idx
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
                m_read_buf[m_checked_index++]='\0'; //TODO
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
        //return LINE_OPEN;

    }
    return LINE_OPEN;
    //return LINE_OK;
}
/*
/ 解析一行，判断依据\r\n
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for ( ; m_checked_idx < m_read_idx; ++m_checked_idx ) {
        temp = m_read_buf[ m_checked_idx ];
        if ( temp == '\r' ) {
            if ( ( m_checked_idx + 1 ) == m_read_idx ) {
                return LINE_OPEN;
            } else if ( m_read_buf[ m_checked_idx + 1 ] == '\n' ) {
                m_read_buf[ m_checked_idx++ ] = '\0';
                m_read_buf[ m_checked_idx++ ] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if( temp == '\n' )  {
            if( ( m_checked_idx > 1) && ( m_read_buf[ m_checked_idx - 1 ] == '\r' ) ) {
                m_read_buf[ m_checked_idx-1 ] = '\0';
                m_read_buf[ m_checked_idx++ ] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}
*/

//解析请求首行：获取请求方法，目标URL，HTTP版本
http_conn::HTTP_CODE http_conn::parse_request_line(char * text)
{
    //使用正则表达式 进行处理 或 传统字符串操作匹配
    // GET /index.html HTTP/1.1
    m_url = strpbrk (text, " \t");   //检验 \n 的位置
    
    if (! m_url) 
    { 
        return BAD_REQUEST;
    }

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
        text+=strspn(text," \t");
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

// 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
// 映射到内存地址m_file_address处，并告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request()
{
    //  /root/webserver/resources
    strcpy(m_real_file,doc_root);
    int len = strlen(doc_root);
    //拼接上本地资源
    strncpy(m_real_file+len,m_url,FILENAME_LEN-len-1);

    //获取 m_real_file 文件相关的状态信息  -1 失败，0 成功
    if(stat(m_real_file,&m_file_stat)<0)
    {
        return NO_RESOURCE;
    }

    //判断访问权限 访问权限
    if(!(m_file_stat.st_mode & S_IROTH))    
    {
        return  FORBIDDEN_REQUEST;
    }

    //判断是否是目录  不能返回目录，要返回文件
    if(S_ISDIR (m_file_stat.st_mode))
    {
        return BAD_REQUEST;
    }

    //以只读方式打开文件
    int fd= open(m_real_file,O_RDONLY);

    //创建内存映射，将资源文件进行内存映射
    m_file_address = (char *) mmap(0,m_file_stat.st_size,PROT_READ,MAP_PRIVATE,fd,0);
    close (fd);
    return FILE_REQUEST;
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
                break;
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


//向写缓冲中 写入待发送的数据
bool http_conn::add_response(const char * format, ...)
{
    if(m_write_idx >= WRITE_BUFFER_SIZE)
    {
        return false;
    }

    va_list arg_list;   //携带参数
    va_start(arg_list,format);
    int len = vsnprintf(m_write_buf+m_write_idx,WRITE_BUFFER_SIZE-1-m_write_idx,format,arg_list);

    if(len>=(WRITE_BUFFER_SIZE-1-m_write_idx))
    {
        return false;
    }

    //索引位置
    m_write_idx += len;
    //放到写缓冲区中
    va_end(arg_list);
    return true;
}

//添加响应首行
bool http_conn::add_status_line(int status,const char * title)
{

    return add_response("%s %d %s\r\n","HTTP/1.1",status,title);

}

//这里做简化操作，仅添加简单的信息
bool http_conn::add_headers(int content_len)
{
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();

    return true;
}

bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length: %d\r\n", content_len);
}

bool http_conn::add_linger()
{
    return add_response("Connection: %s\r\n",(m_linger==true)?"keep-alive":"close");

}

bool http_conn::add_blank_line()
{
    return add_response("%s","\r\n");
}

bool http_conn::add_content(const char * content)
{
    
    return add_response("%s",content);
}

bool http_conn::add_content_type()
{
    //这里仅仅 返回 html类型
    return add_response("Content-Type: %s\r\n","text/html");
}

//对内存映射进行 munmap操作，释放资源
void http_conn::unmap()
{
    if(m_file_address)
    {
        munmap(m_file_address,m_file_stat.st_size);
        m_file_address = 0;
    }
}

//根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
    case INTERNAL_ERROR:
        add_status_line(500,error_500_title);
        add_headers(strlen(error_500_form));
        if(!add_content(error_500_form))
        {
            return false;
        }
        break;
    case BAD_REQUEST:
        add_status_line(400,error_400_title);
        add_headers(strlen(error_400_form));
        if(!add_content(error_400_form))
        {
            return false;
        }
        break;
    case NO_RESOURCE:
        //添加响应首行
        add_status_line(404,error_404_title);
        add_headers(strlen(error_404_form));
        if(!add_content(error_404_form))
        {
            return false;
        }
        break;
    case FORBIDDEN_REQUEST:
            add_status_line( 403, error_403_title );
            add_headers(strlen( error_403_form));
            if ( ! add_content( error_403_form ) ) 
            {
                return false;
            }
            break;
    case FILE_REQUEST:
        add_status_line(200,ok_200_title);
        add_headers(m_file_stat.st_size);
        m_iv[0].iov_base = m_write_buf;
        m_iv[0].iov_len = m_write_idx;
        m_iv[1].iov_base=m_file_address;
        m_iv[1].iov_len=m_file_stat.st_size;
        m_iv_count=2;
        return true;
        break;
    
    default:
    return false;
        break;
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len=m_write_idx;
    m_iv_count = 1;
    return true;
}


//非堵塞，一次性写入数据
bool http_conn::write()
{
    int temp = 0;
    //已经发送的字节
    int bytes_have_send=0;
    //将要发送的字节 m_write_idx写缓冲区中待发送的字节数
    int bytes_to_send = m_write_idx;

    if(bytes_to_send == 0)
    {
        //将要发送的字节数为0，就一次响应结束
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        init();
        return true;
    }

    while (1)
    {
        //分散写：在多块内存中分散填写
        temp = writev(m_sockfd,m_iv,m_iv_count);
        if(temp<=-1)
        {
            //如果 TCP 写缓冲没有空间，则等待下一轮 EPOLLOUT 事件，
            //虽然在此期间 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性
            if(errno == EAGAIN)
            {
                modfd (m_epollfd,m_sockfd,EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
        if(bytes_to_send <= bytes_have_send)
        {
            //发送 HTTP 响应成功，根据 HTTP 请求中的Connection 字段决定是否理解关闭连接
            unmap();
            if(m_linger)
            {
                //重置监听事件
                init();
                modfd(m_epollfd,m_sockfd,EPOLLIN);
                return true;
            }
            else 
            {
                modfd(m_epollfd,m_sockfd,EPOLLIN);
                return false;
            }
        }
        
    }

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
    bool write_ret = process_write(read_ret);
    if(!write_ret)
    {
        //无法写就关闭连接
        close_conn();
    }

    //监听是否可以写
    modfd(m_epollfd,m_sockfd,EPOLLOUT);
}




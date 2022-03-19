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
#include <string.h>
#include "thread_pool/locker.h"
#include <sys/uio.h>

class http_conn
{
public:
    //所有的socket 上的事件，都要被注册到同一个epoll 对象上
    static int m_epollfd;
    //统计所有用户的数量
    static int m_user_count;
    // 文件名的最大长度
    static const int FILENAME_LEN = 200;        

    //读 缓冲大小
    static const int READ_BUFFER_SIZE = 2048 ;
    //写缓冲 大小
    static const int WRITE_BUFFER_SIZE = 1024 ;

    // HTTP请求方法，由枚举定义 这里只支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};
    
    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在解析请求体
    */
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };
    
    /*
        服务器处理HTTP请求的可能结果，报文解析的结果，这里仅列举最常见的
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, 
        FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };
    
    // 从状态机的三种可能状态，即行的读取状态，分别表示
    // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整，正在监测中
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };


    //构造
    http_conn();

    //处理客户端 请求和响应
    void process();

    //初始化新接收的连接
    void init(int sockfd, const sockaddr_in & addr);

    //处理 关闭连接
    void close_conn();

    //非阻塞 一次性读入数据
    bool read();

    //非堵塞，一次性写入数据
    bool write();

    //析构
    ~http_conn();

private:
    //该 HTTP 连接的socket
    int m_sockfd;

    //连接信息, 通信的socket信息
    sockaddr_in m_address;

    //读缓冲区
    char m_read_buf[READ_BUFFER_SIZE];

    //表示读缓冲区中已经读入的客户端数据的最后一个字节的下一个位置
    int m_read_idx;

    //当前正在分析的字符在缓冲区的位置
    int m_checked_index;

    //当前正在解析的行的起始位置
    int m_start_line;

    //请求目标文件的文件名
    char * m_url;
    //请求的协议版本，目前仅支持 HTTP1.1
    char * m_version;
    //请求方法
    METHOD m_method;
    //主机名
    char * m_host;
    //判断HTTP 请求是否保持连接信息
    bool m_linger;
    //HTTP 请求的消息总长度
    int m_content_length;

    //写缓冲区
    char m_write_buf [WRITE_BUFFER_SIZE];
    
    //写缓冲中待发送的字节数
    int m_write_idx;

    //客户请求的目标文件被 mmap到内存中的起始位置
    char * m_file_address;

    //目标文件的状态，通过这个变量来判断文件是否存在，是否为目录，是否可读，并获取文件大小等信息
    struct stat m_file_stat;

    //采用 writev 来执行写操作，定义一下两各成员变量， m_iv_count 表示被写内存块的数量
    struct iovec m_iv[2];
    int m_iv_count;

    //客户请求的目标文件的完整路径，其内容等于 doc_root + m_rul, doc_root作为网站的根目录
    char m_real_file [ FILENAME_LEN ];
    

    //读状态机, 主状态机所处的状态
    CHECK_STATE m_check_state;

    //初始化以上的（其他信息）
    void init();    //函数重载

    //解析HTTP 请求
    HTTP_CODE process_read();

    //填充 HTTP 应答
    bool process_write(HTTP_CODE ret);

    //做具体响应处理
    HTTP_CODE do_request();

/* 被 process_read 调用的，用来分析 HTTP 请求*/
    //解析请求首行
    HTTP_CODE parse_request_line(char * text);

    //解析请求头
    HTTP_CODE parse_headers(char * text);

    //解析请求体
    HTTP_CODE parse_content(char * text);

    //解析某一行数据 根据换行判断，再根据这行的状态，来交给上面对应的处理函数
    LINE_STATUS parse_line();

/* 被 process_read 调用的，用来分析 HTTP 请求 */

/* 被process_write 调用来填充 HTTP 应答 */
    //关闭内存映射
    void unmap();
    //向写缓冲中 写入待发送的数据
    bool add_response(const char * format, ...);
    //添加响应体内容
    bool add_content(const char * content );
    //添加响应类型
    bool add_content_type();
    //添加状态行
    bool add_status_line(int status, const char * title);
    //添加响应头
    bool add_headers(int content_length);
    //添加内容长度
    bool add_content_length(int content_length);
    //添加连接状态
    bool add_linger();
    //
    bool add_blank_line();
/* 被process_write 调用来填充 HTTP 应答 */

    //获取一行数据  内联函数
    char * get_line(){ return m_read_buf + m_start_line; }

};


#endif
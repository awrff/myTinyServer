#ifndef __HTTPCONNECTION__
#define __HTTPCONNECTION__

#include <stdio.h>
#include <stdlib.h>
#include <map>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/uio.h>
#include "../locker/locker.h"
#include "../mysql/sql_conn_pool.h"

class http_conn{
    public:
        static const int FILENAME_LEN = 200; // 文件名最大长度
        static const int READ_BUFFER_SIZE = 2048; // 读缓冲区大小
        static const int WRITE_BUFFER_SIZE = 1024; // 写缓冲区大小
        enum METHOD { GET=0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH}; //HTTP请求方法
        enum CHECK_STATE { CHECK_STATE_REQUESTLINE=0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT}; // 主状态机状态
        enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, \
                            INTERNAL_ERROR, CLOSED_CONNECTION}; // 服务器处理HTTP请求可能返回的结果
        enum LINE_STATUS { LINE_OK=0, LINE_BAD, LINE_OPEN}; // 行读取状态
public:
    http_conn() {}
    ~http_conn() {}

public:
    void init(int sockfd, const sockaddr_in &addr); // 初始化新接受的连接
    void close_conn(bool real_close = true); // 关闭连接
    void process(); // 处理客户请求
    bool read(); // 非阻塞读
    bool write(); // 非阻塞写
    sockaddr_in* get_address()
    {
        return &m_address;
    }
    void init_mysql_result(connection_pool * conn);

private:
    void init(); // 初始化连接
    HTTP_CODE process_read(); // 解析HTTP请求
    bool process_write(HTTP_CODE ret); // 填充HTTP应答

    // 下面这组函数用于process_read解析HTTP请求的过程
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    HTTP_CODE do_request();
    char *get_line() { return m_read_buf + m_start_line; };
    LINE_STATUS parse_line();

    // 下面这组函数用于process_write填充HTTP应答的过程
    void unmap();
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    // 静态的epollfd: 所有的socket上的事件由一个epoll内核管理
    static int m_epollfd;
    // 统计用户的数量
    static int m_user_count;
    MYSQL* mysql;

private:
    int m_sockfd;
    sockaddr_in m_address;
    char m_read_buf[READ_BUFFER_SIZE];
    int m_read_idx;
    int m_checked_idx;
    int m_start_line;
    char m_write_buf[WRITE_BUFFER_SIZE];
    int m_write_idx;
    CHECK_STATE m_check_state;
    METHOD m_method;
    char m_real_file[FILENAME_LEN];
    char *m_url;
    char *m_version;
    char *m_host;
    int m_content_length;
    bool m_linger;
    char *m_file_address;
    struct stat m_file_stat;
    struct iovec m_iv[2];
    int m_iv_count;
    char *m_string; //存储请求头数据
    int cgi; // 是否使用POST
};   

#endif
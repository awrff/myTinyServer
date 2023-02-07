#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <error.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include "timer/lst_timer.h"
#include "threadpool/threadpool.h"
#include "threadpool/threadpool_wosql.h"
#include "http/http_conn.h"
#include "mysql/sql_conn_pool.h"

#define CHAT_PORT 10992
#define HTTP_PORT 10993

#define TIME_SLOT 5 // 最小超时单位
#define MAX_REQUESTS 100
#define MAX_EPOLL_SIZE 100
#define MAX_CLNT_NUM 100
#define BUFF_SIZE 500

extern int addfd(int epollfd, int fd, bool one_shot);
extern int setnonblocking(int fd);

// 定时器参数
// 用static声明的全局变量称之为静态外部变量
static int epfd = 0;
static int pipefd[2];
static sort_timer_lst timer_lst;

void error_handling(const char* message);
void* handle_clnt(void* arg);
void show_error(int connfd, const char* info);

//信号处理函数
void sig_handler(int sig){
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char*) &msg, 1, 0);
    errno = save_errno;
}

//设置信号处理函数
void addsig(int sig, void(handler)(int), bool restart=true){
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler; // 绑定信号处理函数
    if(restart){
        sa.sa_flags |= SA_RESTART; // 指定信号处理行为， 这里是使被信号打断的系统调用自动重新发起
    }
    sigfillset(&sa.sa_mask); // 指定需要屏蔽的信号
    assert(sigaction(sig, &sa, NULL) != -1);
}

// 定时触发，并重新计时
void timer_handler(){
    timer_lst.tick();
    alarm(TIME_SLOT);
}

// 删除不活动的连接
void cb_func(client_data* user_data){
    epoll_ctl(epfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;
}

int main(int argc, char* argv[]){
    int serv_sock, clnt_sock;
    struct sockaddr_in serv_addr;
    socklen_t clnt_addr_sz;
    pthread_t t_id;

    /* 忽略SIGPIPE信号：SIGPIPE信号发生在通信双方有一方断开连接之后，另一方如果继续写入数据时，默认
        处理方式是终止程序，这对服务器程序来说是不可取的 */
    addsig(SIGPIPE, SIG_IGN);

    // 创建数据库连接池
    connection_pool* connPool = connection_pool::GetInstance();
    connPool->init("localhost", "root", "", "yourdb", 3306, 8);

    threadpool<http_conn>* http_pool;
    try{
        http_pool = new threadpool<http_conn>(connPool);
    }
    catch(...){
        return 1;
    }
    
    // 读取用户表
    http_conn* http_clnts = new http_conn[MAX_CLNT_NUM];
    http_clnts->init_mysql_result(connPool);

    if(argc != 2){
        printf(" Usage : %s <port> \n", argv[0]);
        exit(1);
    }
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));

    serv_sock = socket(PF_INET, SOCK_STREAM, 0);
    if(serv_sock == -1) {
        error_handling("socket error");
    }

    /* SO_LINGER选项用来设置延迟关闭的时间，等待套接字发送缓冲区中的数据发送完成。没有设置该选项时，在调用close()后，在
    发送完FIN后会立即进行一些清理工作并返回。如果设置了SO_LINGER选项，并且等待时间为正值，则在清理之前会等待一段时间。 */
    struct linger tmp={1, 0};
    setsockopt(serv_sock, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

    if(bind(serv_sock, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) == -1){
        error_handling("bind error");
    }

    if(listen(serv_sock, MAX_REQUESTS) == -1){
        error_handling("listen error");
    }
    
    struct epoll_event* ep_events;
    struct epoll_event event;
    int event_cnt;

    epfd = epoll_create(MAX_EPOLL_SIZE);
    ep_events = (epoll_event*)malloc(MAX_EPOLL_SIZE*sizeof(event));
    http_conn::m_epollfd = epfd; // 初始化http_conn的epollfd
    addfd(epfd, serv_sock, false);
    // event.events = EPOLLIN;
    // event.data.fd = serv_sock;
    // epoll_ctl(epfd, EPOLL_CTL_ADD, serv_sock, &event);
    
    // 定时器：用管道创建套接字
    int ret;
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    if(ret == -1){
        error_handling("socketpair error");
    }
    setnonblocking(pipefd[1]); // 将写端设置为非阻塞，防止缓冲区满以后无法再写于是阻塞的情况
    addfd(epfd, pipefd[0], false); // 统一事件源，将读端注册为epoll事件进行检测

    // 注册两个信号一个由alarm函数引起, 一个由终止(ctrl+c)引起, sig_handler会将信号写入管道
    addsig(SIGALRM, sig_handler, false);
    addsig(SIGTERM, sig_handler, false);

    // 用户定时器的队列
    client_data* users_timer = new client_data[MAX_CLNT_NUM];

    bool timeout = false;
    alarm(TIME_SLOT);

    bool stop_server = false;

    while(!stop_server){
        event_cnt = epoll_wait(epfd, ep_events, MAX_EPOLL_SIZE, -1);
        /* EINTR:当阻塞于某个慢系统调用的一个进程捕获某个信号且相应信号处理函数返回时，该系统调用可能
        返回一个EINTR错误，这里指epoll_wait在阻塞时遇到了alarm触发的SIGALRM信号 */
        if(event_cnt == -1 && errno != EINTR){
            puts("epoll_wait error");
            break;
        }
        
        for(int i=0; i<event_cnt; i++){
            int sockfd = ep_events[i].data.fd;
            if(sockfd == serv_sock){
                printf("CONNECT EVENT\n");
                struct sockaddr_in clnt_addr;
                clnt_addr_sz = sizeof(clnt_addr);
                clnt_sock = accept(serv_sock, (struct sockaddr*) &clnt_addr, &clnt_addr_sz);
                if(clnt_sock < 0){
                    printf("accept error");
                    continue;
                }
                printf("<html> new client connected, ip address: %s\n", inet_ntoa(clnt_addr.sin_addr));
                if(http_conn::m_user_count >= MAX_CLNT_NUM){
                    show_error(sockfd, "interal server busy");
                    continue;
                }
                http_clnts[clnt_sock].init(clnt_sock, clnt_addr);

                // 定时器部分
                users_timer[clnt_sock].address = clnt_addr;
                users_timer[clnt_sock].sockfd = clnt_sock;

                util_timer* timer = new util_timer();
                timer->user_data = &users_timer[clnt_sock];
                timer->cb_func = cb_func;

                time_t cur = time(NULL);
                timer->expire = cur + 3*TIME_SLOT;
                users_timer[clnt_sock].timer = timer;

                timer_lst.add_timer(timer);
            }
            else if(ep_events[i].events & (EPOLLRDHUP | EPOLLHUP, EPOLLERR)){
                printf("EPOLL ERROR\n");
                // 关闭连接需要移除定时器
                util_timer* timer = users_timer[sockfd].timer;
                timer->cb_func(&users_timer[sockfd]);
                if(timer){
                    timer_lst.del_timer(timer);
                }
            }
            else if((sockfd == pipefd[0]) && (ep_events[i].events & EPOLLIN)){
                // 处理信号 SIGALRM & SIGTERM
                printf("EPOLL SIG\n");
                int sig;
                char singals[1024];
                ret = recv(pipefd[0], singals, sizeof(singals), 0);
                if(ret == -1){
                    continue;
                }
                else if(ret == 0){
                    continue;
                }
                else{
                    for(int i=0; i<ret; i++){
                        switch(singals[i]){
                            case SIGALRM:
                            {
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
            else if(ep_events[i].events & EPOLLIN){
                printf("EPOLLIN EVENT\n");
                util_timer* timer = users_timer[sockfd].timer;
                if(http_clnts[sockfd].read()){
                    // 如果是读入数据，定时器要刷新
                    http_pool->append(http_clnts+sockfd);
                    if(timer){
                        time_t cur = time(NULL);
                        timer->expire = cur + 3*TIME_SLOT;
                        timer_lst.adjust_timer(timer);
                    }
                }
                else{
                    // 如果是断开连接，则去掉定时器
                    timer->cb_func(&users_timer[sockfd]);
                    if(timer){
                        timer_lst.del_timer(timer);
                    }
                }
            }
            else if(ep_events[i].events & EPOLLOUT){
                // 写入数据也要刷新定时器
                util_timer *timer = users_timer[sockfd].timer;
                printf("EPOLLOUT EVENT\n");
                if(!http_clnts[sockfd].write()){
                    // write是根据keep-alive决定返回值的
                    // write返回true表示keep-alive生效，刷新定时器
                    // 返回false表示不保留连接
                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIME_SLOT;
                        timer_lst.adjust_timer(timer);
                    }
                }
                else{
                    timer->cb_func(&users_timer[sockfd]);
                    if (timer)
                    {
                        timer_lst.del_timer(timer);
                    }
                }
            }
        }
        // 在每一轮epoll_wait之后检查是否超时
        if(timeout){
            timer_handler();
            timeout = false;
        }
    }
    close(epfd);
    close(serv_sock);
    close(pipefd[1]);
    close(pipefd[0]);
    delete http_pool;
    delete http_clnts;
    delete users_timer;
    return 0;
}
void error_handling(const char* message){
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}
void show_error(int connfd, const char* info){
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include "threadpool/threadpool.h"
#include "chat/chat.h"
#include "http/http_conn.h"

#define CHAT_PORT 10992
#define HTTP_PORT 10993

#define MAX_REQUESTS 100
#define MAX_EPOLL_SIZE 100
#define MAX_CLNT_NUM 100
#define BUFF_SIZE 500

void error_handling(const char* message);
void* handle_clnt(void* arg);
void show_error(int connfd, const char* info);
void addsig(int sig, void(handler)(int), bool restart=true){
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if(restart){
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

int clnt_socks[MAX_CLNT_NUM];
int clnt_cnt = 0;
threadpool<chat> pool;

int main(int argc, char* argv[]){
    int serv_sock, clnt_sock;
    struct sockaddr_in serv_addr;
    socklen_t clnt_addr_sz;
    pthread_t t_id;

    /* 忽略SIGPIPE信号：SIGPIPE信号发生在通信双方有一方断开连接之后，另一方如果继续写入数据时，默认
        处理方式是终止程序，这对服务器程序来说是不可取的 */
    addsig(SIGPIPE, SIG_IGN);

    http_conn http_clnts[MAX_CLNT_NUM];
    threadpool<http_conn>* http_pool;
    try{
        http_pool = new threadpool<http_conn>;
    }
    catch(...){
        return 1;
    }

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
    int epfd, event_cnt;

    epfd = epoll_create(MAX_EPOLL_SIZE);
    ep_events = (epoll_event*)malloc(MAX_EPOLL_SIZE*sizeof(event));
    http_conn::m_epollfd = epfd; // 初始化http_conn的epollfd

    event.events = EPOLLIN;
    event.data.fd = serv_sock;
    epoll_ctl(epfd, EPOLL_CTL_ADD, serv_sock, &event);

    while(1){
        event_cnt = epoll_wait(epfd, ep_events, MAX_EPOLL_SIZE, -1);
        if(event_cnt == -1){
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
                if(ntohs(serv_addr.sin_port) == CHAT_PORT){
                    printf("<chat> new client connected, ip address: %s\n", inet_ntoa(clnt_addr.sin_addr));
                    chat chat_clnt(clnt_sock, &clnt_cnt, clnt_socks);
                    pool.append(&chat_clnt);
                }
                else if(ntohs(serv_addr.sin_port) == HTTP_PORT){
                    printf("<html> new client connected, ip address: %s\n", inet_ntoa(clnt_addr.sin_addr));
                    if(http_conn::m_user_count >= MAX_CLNT_NUM){
                        show_error(sockfd, "interal server busy");
                        continue;
                    }
                    http_clnts[clnt_sock].init(clnt_sock, clnt_addr);
                }
                else{
                    error_handling("no matching ports");
                }
            }
            else if(ep_events[i].events & (EPOLLRDHUP | EPOLLHUP, EPOLLERR)){
                printf("EPOLL ERROR\n");
                http_clnts[sockfd].close_conn();
            }
            else if(ep_events[i].events & EPOLLIN){
                printf("EPOLLIN EVENT\n");
                if(http_clnts[sockfd].read()){
                    http_pool->append(http_clnts+sockfd);
                }
                else{
                    http_clnts[sockfd].close_conn();
                }
            }
            else if(ep_events[i].events & EPOLLOUT){
                printf("EPOLLOUT EVENT\n");
                if(!http_clnts[sockfd].write()){
                    http_clnts[sockfd].close_conn();
                }
            }
        }
    }
    close(epfd);
    close(serv_sock);
    delete http_pool;
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

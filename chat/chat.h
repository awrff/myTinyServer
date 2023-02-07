#ifndef __CHAT__
#define __CHAT__

#define MAX_CLNT_NUM 100
#define BUFF_SIZE 500
#define TIME_SLOT 5
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "../timer/lst_timer.h"

class chat{
    private:
        int clnt_sock;
        char buf[BUFF_SIZE];
        chat_client_data* clnt_data;

        int *clnt_cnt;
        int *clnt_socks;
        pthread_mutex_t* mutx;

        chat_util_timer* timer;
        chat_sort_timer_lst* timer_list;
    public:
        chat(int sock, chat_client_data* clnt_data, int* cnt, int* clnt_socks, pthread_mutex_t* mutx, \
            chat_util_timer* timer, chat_sort_timer_lst* timer_list): clnt_sock(sock), clnt_cnt(cnt), \
            clnt_data(clnt_data), clnt_socks(clnt_socks), mutx(mutx), timer(timer), timer_list(timer_list)
            {
                pthread_mutex_lock(mutx);
                clnt_socks[*clnt_cnt] = clnt_sock;
                (*clnt_cnt) += 1;
                pthread_mutex_unlock(mutx);
            }
        void process(){
            int str_len;
            int name_len = 0;
            while( (str_len = read(clnt_sock, buf, sizeof(buf))) != 0){
                // 发送消息给其他用户
                pthread_mutex_lock(mutx);
                for(int i=0; i<(*clnt_cnt); i++){
                    write(clnt_socks[i], buf, str_len);
                }
                pthread_mutex_unlock(mutx);

                // 服务器端打印消息
                buf[str_len] = 0;
                printf("%s", buf);

                // 更新计时器
                time_t cur = time(NULL);
                timer->expire = cur + 3 * TIME_SLOT;
                timer_list->adjust_timer(timer);
            }
            // 这里可能有两种情况, 一是服务器定时器触发客户端发送EOF, 定时器已经执行过一遍cb_func
            // 二是客户端主动发送EOF，定时器还未超时
            time_t cur = time(NULL);
            if(cur < timer->expire){
                timer->cb_func(clnt_data);
                if(timer){
                    timer_list->del_timer(timer);
                }
            }
            close(clnt_sock);
        }
};
#endif
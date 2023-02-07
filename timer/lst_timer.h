#ifndef __LST_TIMER__
#define __LST_TIMER__

#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define NAME_SIZE 20
class util_timer;
class client_data{
    public:
        sockaddr_in address;
        int sockfd;
        util_timer* timer; // 每个连接保存一个定时器
};
class util_timer{
    public:
        util_timer() : prev(NULL), next(NULL) {}

    public:
        // 以链表形式进行管理
        util_timer* prev;
        util_timer* next;
        time_t expire;
        void (*cb_func)(client_data *); // 超时的回调函数
        client_data* user_data;
};

class sort_timer_lst{
    public:
        sort_timer_lst(): head(NULL), tail(NULL){}
        ~sort_timer_lst(){
            //析构删除整个链表
            util_timer* tmp = head;
            while(tmp){
                head = tmp->next;
                delete tmp;
                tmp = head;
            }
        }

        void add_timer(util_timer* timer){
            if(!timer){
                return ;
            }
            if(!head){
                head = tail = timer;
                return ;
            }
            if(timer->expire < head->expire){
                timer->next = head;
                head->prev = timer;
                head = timer;
                return ;
            }
            add_timer(timer, head);
        }
        /* 调整的逻辑：
            为空则返回，不为空且expire小于后面也返回
            如果是头部，将head换为下一个timer然后重新加入
            正常情况，去除首尾连接然后重新加入
        */
        void adjust_timer(util_timer* timer){
            if(!timer){
                return ;
            }
            util_timer* tmp = timer->next;
            if(!tmp || (timer->expire < tmp->expire)){
                return ;
            }
            if(timer == head){
                head = head->next;
                head->prev = NULL;
                timer->next = NULL;
                add_timer(timer,head);
            }else{
                timer->prev->next = timer->next;
                // 一个bug?:如果timer是tail，访问timer->next->prev会报错吧
                timer->next->prev = timer->prev;
                add_timer(timer, timer->next);
            }
        }
        void del_timer(util_timer* timer){
            if(!timer){
                return ;
            }
            if((timer == head) && (timer == tail)){
                delete timer;
                head = NULL;
                tail = NULL;
                return ;
            }
            if(timer == head){
                head = head->next;
                head->prev = NULL;
                delete timer;
                return ;
            }
            if(timer == tail){
                tail = tail->prev;
                tail->next = NULL;
                delete timer;
                return ;
            }
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            delete timer;
        }
        void tick(){
            if(!head){
                return ;
            }
            time_t cur = time(NULL);
            util_timer* tmp = head;
            while(tmp){
                if(cur < tmp->expire){
                    break;
                }
                tmp->cb_func(tmp->user_data);
                head = tmp->next;
                if(head){
                    head->prev = NULL;
                }
                delete tmp;
                tmp = head;
            }
        }
    private:
        void add_timer(util_timer *timer, util_timer *lst_head){
            util_timer* prev = lst_head;
            util_timer* tmp = prev->next;
            while(tmp){
                if(timer->expire < tmp->expire){
                    prev->next = timer;
                    timer->prev = prev;
                    timer->next = tmp;
                    tmp->prev = timer;
                    break;
                }
                prev = tmp;
                tmp = tmp->next;
            }
            if(!tmp){
                prev->next = timer;
                timer->prev = prev;
                timer->next = NULL;
                tail = timer;
            }
        }
    private:
        util_timer* head;
        util_timer* tail;
};


class chat_util_timer;
class chat_client_data{
    public:
        char name[NAME_SIZE];
        sockaddr_in address;
        int sockfd;
        chat_util_timer* timer;
};
class chat_util_timer{
    public:
        chat_util_timer* prev;
        chat_util_timer* next;
        time_t expire;
        void (*cb_func)(chat_client_data *); // 重载回调函数s
        chat_client_data* chat_user_data;
};
class chat_sort_timer_lst{
    public:
        chat_sort_timer_lst(): head(NULL), tail(NULL){}
        ~chat_sort_timer_lst(){
            //析构删除整个链表
            chat_util_timer* tmp = head;
            while(tmp){
                head = tmp->next;
                delete tmp;
                tmp = head;
            }
        }

        void add_timer(chat_util_timer* timer){
            if(!timer){
                return ;
            }
            if(!head){
                head = tail = timer;
                return ;
            }
            if(timer->expire < head->expire){
                timer->next = head;
                head->prev = timer;
                head = timer;
                return ;
            }
            add_timer(timer, head);
        }
        /* 调整的逻辑：
            为空则返回，不为空且expire小于后面也返回
            如果是头部，将head换为下一个timer然后重新加入
            正常情况，去除首尾连接然后重新加入
        */
        void adjust_timer(chat_util_timer* timer){
            if(!timer){
                return ;
            }
            chat_util_timer* tmp = timer->next;
            if(!tmp || (timer->expire < tmp->expire)){
                return ;
            }
            if(timer == head){
                head = head->next;
                head->prev = NULL;
                timer->next = NULL;
                add_timer(timer,head);
            }else{
                timer->prev->next = timer->next;
                // 一个bug?:如果timer是tail，访问timer->next->prev会报错吧
                timer->next->prev = timer->prev;
                add_timer(timer, timer->next);
            }
        }
        void del_timer(chat_util_timer* timer){
            if(!timer){
                return ;
            }
            if((timer == head) && (timer == tail)){
                delete timer;
                head = NULL;
                tail = NULL;
                return ;
            }
            if(timer == head){
                head = head->next;
                head->prev = NULL;
                delete timer;
                return ;
            }
            if(timer == tail){
                tail = tail->prev;
                tail->next = NULL;
                delete timer;
                return ;
            }
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            delete timer;
        }
        void tick(){
            if(!head){
                return ;
            }
            time_t cur = time(NULL);
            chat_util_timer* tmp = head;
            while(tmp){
                if(cur < tmp->expire){
                    break;
                }
                tmp->cb_func(tmp->chat_user_data);
                head = tmp->next;
                if(head){
                    head->prev = NULL;
                }
                delete tmp;
                tmp = head;
            }
        }
    private:
        void add_timer(chat_util_timer *timer, chat_util_timer *lst_head){
            chat_util_timer* prev = lst_head;
            chat_util_timer* tmp = prev->next;
            while(tmp){
                if(timer->expire < tmp->expire){
                    prev->next = timer;
                    timer->prev = prev;
                    timer->next = tmp;
                    tmp->prev = timer;
                    break;
                }
                prev = tmp;
                tmp = tmp->next;
            }
            if(!tmp){
                prev->next = timer;
                timer->prev = prev;
                timer->next = NULL;
                tail = timer;
            }
        }
    private:
        chat_util_timer* head;
        chat_util_timer* tail;
};

#endif

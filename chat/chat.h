#ifndef __CHAT__
#define __CHAT__

#define MAX_CLNT_NUM 100
#define BUFF_SIZE 500
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>


class chat{
    private:
        int clnt_sock;
        char buf[BUFF_SIZE];
        static int clnt_cnt;
        static int* clnt_socks;
        static pthread_mutex_t mutx;
    public:
        chat(int sock): clnt_sock(sock) {
            pthread_mutex_lock(&mutx);
            clnt_socks[clnt_cnt++] = sock;
            pthread_mutex_unlock(&mutx);
        }
        void process(){
            int str_len;
            int name_len = 0;
            char name[20];
            while( (str_len = read(clnt_sock, buf, sizeof(buf))) != 0){
                pthread_mutex_lock(&mutx);
                for(int i=0; i<clnt_cnt; i++){
                    write(clnt_socks[i], buf, str_len);
                }
                buf[str_len] = 0;
                printf("%s", buf);
                
                name_len = strchr(buf, ']')-buf;
                strncpy(name, buf+1, name_len);
                name[name_len-1] = 0;

                pthread_mutex_unlock(&mutx);       
            }
            pthread_mutex_lock(&mutx);
            clnt_cnt--;
            for(int i=0; i<clnt_cnt; i++){
                if(clnt_socks[i] == clnt_sock){
                    for(int j=i; j<clnt_cnt; j++)
                        clnt_socks[j] = clnt_socks[j+1];
                    break;
                }
            }
            pthread_mutex_unlock(&mutx);
            if(name_len != 0) printf("client %s left.\n", name);
            else printf("client left\n");
            close(clnt_sock);
        }
};
pthread_mutex_t chat::mutx= PTHREAD_MUTEX_INITIALIZER;
int chat::clnt_cnt = 0;
int* chat::clnt_socks = new int[MAX_CLNT_NUM];
#endif
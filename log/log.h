#ifndef __LOG__
#define __LOG__

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"

using namespace std;
class Log{
    public:
        // 单例模式-懒汉，在c++11之前应该使用双层加锁的模式，c++11之后可以用返回局部静态变量的形式
        static Log* get_instance(){
            static Log instance;
            return &instance;
        }
        
        // 用作线程参数的回调函数
        static void* flush_log_thread(void* args){
            Log::get_instance()->async_write_log();
            return (void*)NULL;
        }

        bool init(const char* file_name, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);

        void write_log(int level, const char* format, ...);

        void flush(void);

    private:
        Log();
        virtual ~Log();
        void* async_write_log(){
            string single_log;
            
            // 从阻塞队列中取出一条日志
            while(m_log_queue->pop(single_log)){
                m_mutex.lock();
                fputs(single_log.c_str(), m_fp);
                m_mutex.unlock();
            }

            return (void*) NULL;
        }
    private:
        char dir_name[128]; //路径名
        char log_name[128]; //log文件名
        int m_split_lines;  //日志最大行数
        int m_log_buf_size; //日志缓冲区大小
        long long m_count;  //日志行数记录
        int m_today;        //因为按天分类,记录当前时间是那一天
        FILE *m_fp;         //打开log的文件指针
        char *m_buf;
        block_queue<string> *m_log_queue; //阻塞队列
        bool m_is_async;                  //是否同步标志位
        locker m_mutex;
};

#define LOG_DEBUG(format, ...) Log::get_instance()->write_log(0, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) Log::get_instance()->write_log(1, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) Log::get_instance()->write_log(2, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) Log::get_instance()->write_log(3, format, ##__VA_ARGS__)

#endif
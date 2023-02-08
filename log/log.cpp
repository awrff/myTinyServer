#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include <pthread.h>
#include "log.h"

using namespace std;

Log::Log(){
    m_count = 0;
    m_is_async = false; // 默认同步模式
}

Log::~Log(){
    if(m_fp != NULL){
        fclose(m_fp);
    }
}

bool Log::init(const char *file_name, int log_buf_size, int split_lines, int max_queue_size){
    // 最大长度不为0表示是异步
    if(max_queue_size >= 1){
        m_is_async = true;
        m_log_queue = new block_queue<string>(max_queue_size);
        pthread_t tid;
        //flush_log_thread为回调函数,这里表示创建线程异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }
    // 日志文件缓冲区
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);
    m_split_lines = split_lines;
    
    // 当前时间
    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
 
    const char *p = strrchr(file_name, '/');
    char log_full_name[256] = {0};

    // 如果日志文件不存在
    if(p == NULL){
        // 用时间加名字命名
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }else{
        // p的位置是日志文件和目录中间的/符号
        strcpy(log_name, p + 1);
        strncpy(dir_name, file_name, p - file_name + 1);
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;
    m_fp = fopen(log_full_name, "a");
    if(m_fp == NULL){
        return false;
    }
    return true;
}

void Log::write_log(int level, const char* format, ...){
    // 获取当前时间
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm* sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};
    switch(level){
        case 0:
        {
            strcpy(s, "[debug]:");
            break;
        }
        case 1:
        {
            strcpy(s, "[info]:");
            break;
        }
        case 2:
        {
            strcpy(s, "[warn]:");
            break;
        }
        case 3:
        {
            strcpy(s, "[erro]:");
            break;
        }
        default:
        {
            strcpy(s, "[info]:");
            break;
        }
    }

    // 开始向阻塞队列里写入消息
    m_mutex.lock();
    m_count++;

    // m_split_lines 最大行数
    // 当今天还没有日志文件生成 或者 当前日志文件已经到达最大行数时，需要创建新的文件
    if(m_today != my_tm.tm_mday || m_count % m_split_lines == 0){
        char new_log[256] = {0};
        fflush(m_fp);
        fclose(m_fp);
        char tail[16] = {0};

        // 文件名的末尾内容：时间
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

        if(m_today != my_tm.tm_mday){
            // 今天没有日志文件， 创建完整的日志文件
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }else{
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a");
    }

    m_mutex.unlock();

    // va_list 可变参数列表
    va_list valst;
    va_start(valst, format);

    string log_str;
    m_mutex.lock();

    //写入的具体时间内容格式
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    
    int m = vsnprintf(m_buf + n, m_log_buf_size - 1, format, valst);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;

    m_mutex.unlock();
 
    // 异步加入阻塞队列，同步直接输出到文件
    if (m_is_async && !m_log_queue->full())
    {
        m_log_queue->push(log_str);
    }
    else
    {
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }

    va_end(valst);
}

void Log::flush(void)
{
    m_mutex.lock();
    //强制刷新写入流缓冲区
    fflush(m_fp);
    m_mutex.unlock();
}
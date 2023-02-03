#ifndef __THREADPOOL_WOSQL__
#define __THREADPOOL_WOSQL__

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../locker/locker.h"

template <typename T>
class threadpool_wosql{
    public:
        threadpool_wosql(int thread_number = 8, int max_requests = 10000);
        ~threadpool_wosql();
        bool append(T* request);
    
    private:
        /* worker函数作为创建线程时的函数指针，传入参数为具体的线程池，然后在worker函数内部调用run()函数 */
        static void* worker(void* arg);
        void run();
    
    private:
    int m_thread_number; // 线程数量
    int m_max_requests; // 最大请求数量
    pthread_t* m_threads;   // 线程数组，大小为m_thread_number
    std::list< T* > m_workqueue; // 请求队列
    /* 用互斥锁和信号量分别控制请求队列和通知有新任务需要处理 */
    locker m_queuelocker; // 互斥锁
    sem m_queuestat; // 是否有任务需要处理
    bool m_stop; // 是否结束线程
    connection_pool *m_connPool = NULL;  //数据库
};

template <typename T>
threadpool_wosql<T>::threadpool_wosql(int thread_number, int max_requests):
    m_thread_number(thread_number), m_max_requests(max_requests), m_stop(false), m_threads(NULL){

    if((m_thread_number <= 0) || (m_max_requests <= 0)){
        throw std::exception();
    }

    m_threads = new pthread_t[m_thread_number];
    if(!m_threads){
        throw std::exception();
    }    

    // 创建thread_number个线程
    for( int i = 0; i < m_thread_number; i++){
        // printf("create the %dth thread.\n", i);
        if(pthread_create(&m_threads[i], NULL, worker, this) != 0){
            delete[] m_threads;
            throw std::exception();
        }
        
        // pthread_detach成功时候返回0，将线程detach之后其结束状态不会被捕捉，从而成为脱离状态
        if(pthread_detach(m_threads[i])){
            delete[] m_threads;
            throw std::exception();
        }
    }
    printf("create %d thread.\n", m_thread_number);
}

template <typename T>
threadpool_wosql<T>::~threadpool_wosql(){
    delete[] m_threads;
    m_stop = true;
}

template <typename T>
bool threadpool_wosql<T>::append(T *request)
{
    m_queuelocker.lock();
    if (m_workqueue.size() > m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template <typename T>
void* threadpool_wosql<T>::worker(void* arg){
    threadpool_wosql* pool = (threadpool_wosql* ) arg;
    pool->run();
    return pool;
}

template <typename T>
void threadpool_wosql<T>::run(){
    while(!m_stop){
        m_queuestat.wait();
        m_queuelocker.lock();
        if(m_workqueue.empty()){
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if(!request){
            continue;
        }
        // 初始化sql连接, connectionRAII管理的数据库连接在mysqlcon这个变量结束时自动销毁, 因此不用手动释放连接
        request->process();
    }
}

#endif
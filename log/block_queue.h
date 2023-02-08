#ifndef __BLOCK_QUEUE__
#define __BLOCK_QUEUE__

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../locker/locker.h"
using namespace std;

template<typename T>
class block_queue{
    public:
        block_queue(int max_size = 1000){
            // max_size = 0表示同步模式
            if(max_size <= 0){
                exit(-1);
            } 
            m_max_size = max_size;
            m_array = new T[max_size];
            m_size = 0;
            m_front = -1;
            m_back = -1;
        }

        void clear(){
            m_mutex.lock();
            m_size = 0;
            m_front = -1;
            m_back = -1;
            m_mutex.unlock();
        }

        ~block_queue(){
            m_mutex.lock();
            if(m_array != NULL){
                delete[] m_array;
            }
            m_mutex.unlock();
        }

        bool full(){
            m_mutex.lock();
            if(m_size >= m_max_size){
                m_mutex.unlock();
                return true;
            }
            m_mutex.unlock();
            return false;
        }

        bool empty(){
            m_mutex.lock();
            if (0 == m_size)
            {
                m_mutex.unlock();
                return true;
            }
            m_mutex.unlock();
            return false;
        }

        //返回队首元素
        bool front(T &value){
            m_mutex.lock();
            if (0 == m_size)
            {
                m_mutex.unlock();
                return false;
            }
            value = m_array[m_front];
            m_mutex.unlock();
            return true;
        }
        //返回队尾元素
        bool back(T &value){
            m_mutex.lock();
            if (0 == m_size)
            {
                m_mutex.unlock();
                return false;
            }
            value = m_array[m_back];
            m_mutex.unlock();
            return true;
        }

        int size(){
            int tmp = 0;

            m_mutex.lock();
            tmp = m_size;

            m_mutex.unlock();
            return tmp;
        }

        int max_size(){
            int tmp = 0;

            m_mutex.lock();
            tmp = m_max_size;

            m_mutex.unlock();
            return tmp;
        }

        // 向队列添加元素，相当于生产者
        bool push(const T& item){
            m_mutex.lock();
            if(m_size >= m_max_size){
                // 如果队列已经满了
                m_cond.broadcast(); // 唤醒消费者来取数据
                m_mutex.unlock();
                return false;
            }

            m_back = (m_back + 1) % m_max_size; // 循环队列
            m_array[m_back] = item;
            m_size++;

            m_cond.broadcast();
            m_mutex.unlock();
            return true;
        }

        // 消费者取数据
        bool pop(T& item){
            m_mutex.lock();
            while(m_size <= 0){
                // 队列中没有数据
                // wait成功会返回0，进入下面的步骤，否则返回false
                // 这个过程是阻塞的，可以用带超时处理的m_cond.timewait()替换
                if(!m_cond.wait(m_mutex.get())){
                    m_mutex.unlock();
                    return false;
                }
            }
            m_front = (m_front + 1) % m_max_size;
            item = m_array[m_front];
            m_size--;
            m_mutex.unlock();
            return true;
        }
    private:
        locker m_mutex;
        cond m_cond;

        T* m_array;
        int m_size;
        int m_max_size;
        int m_front;
        int m_back;
};

#endif
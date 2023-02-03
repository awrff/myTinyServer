#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_conn_pool.h"

using namespace std;

connection_pool::connection_pool(){
    this->CurConn = 0;
    this->FreeConn = 0;
}

connection_pool* connection_pool::GetInstance(){
    static connection_pool connPool;
    return &connPool;
}

void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, unsigned int MaxConn){
    this->url = url;
	this->Port = Port;
	this->User = User;
	this->PassWord = PassWord;
	this->DatabaseName = DBName;

    /* sql连接池和线程池大体上差不多
        创建连接使用mysql_init函数
        初始化使用mysql_read_connect函数
        使用一个链表进行维护 */

    lock.lock();
    for(int i=0; i<MaxConn; i++){
        MYSQL* con = NULL;
        con = mysql_init(con);

        if(con == NULL){
            cout << "Error:" << mysql_error(con);
            exit(1);
        }

        con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

        if (con == NULL)
		{
			cout << "Error: " << mysql_error(con);
			exit(1);
		}
        connList.push_back(con);
        ++FreeConn;
    }

    reserve = sem(FreeConn);
    this->MaxConn = FreeConn;
    lock.unlock();
}

MYSQL* connection_pool::GetConnection(){
    MYSQL* con = NULL;

    if(0 == connList.size()){
        return NULL;
    }

    // 申请锁和信号量
    reserve.wait(); 
    lock.lock();

    // 获取连接
    con = connList.front();
    connList.pop_front();

    // 更改空闲和使用中的连接数量
    --FreeConn;
    ++CurConn;

    // 释放锁和信号量
    lock.unlock();
    return con;
}

bool connection_pool::ReleaseConnection(MYSQL* con){
    if(con == NULL){
        return false;
    }

    lock.lock();

    connList.push_back(con);
	++FreeConn;
	--CurConn;

	lock.unlock();
    reserve.post();
	
    return true;
}

void connection_pool::DestroyPool(){
    lock.lock();
    if(connList.size() > 0){
        list<MYSQL*>::iterator it;
        for(it = connList.begin(); it!= connList.end(); it++){
            MYSQL* con = *it;
            mysql_close(con);
        }

        CurConn = 0;
        FreeConn = 0;
        connList.clear();
    }

    lock.unlock();
}

int connection_pool::GetFreeConn(){
    return this->FreeConn;
}

connection_pool::~connection_pool()
{
	DestroyPool();
}

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool){
	*SQL = connPool->GetConnection();
	
	conRAII = *SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII(){
	poolRAII->ReleaseConnection(conRAII);
}
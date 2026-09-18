#include <string>
#include <iostream>
#include <muduo/net/TcpServer.h>
#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>

#include"../include/ChatServer.h"
#include"../include/AIUtil/EnvUtil.h"

const std::string QUEUE_NAME = "sql_queue";
const int THREAD_NUM = 2;

void executeMysql(const std::string sql) {
    http::MysqlUtil mysqlUtil_;
    mysqlUtil_.executeUpdate(sql);
}


int main(int argc, char* argv[]) {
	LOG_INFO << "pid = " << getpid();
	std::string serverName = "ChatServer";
	int port = envOrInt("HTTP_PORT", 8116);
    int opt;
    const char* str = "p:";
    while ((opt = getopt(argc, argv, str)) != -1)
    {
        /*
            对于 getopt 函数，str字符串含义是如果是 -p 选项后面必须有参数
            参数会存储在 optarg 
            opt 存储的是选项字符即：p
        */
        switch (opt)
        {
        case 'p':
        {
            port = atoi(optarg);
            break;
        }
        default:
            break;
        }
    }
    muduo::Logger::setLogLevel(muduo::Logger::WARN);
    ChatServer server(port, serverName);
    server.setThreadNum(8);
    server.initChatMessage();


    RabbitMQThreadPool pool(envOr("RABBITMQ_HOST", "localhost"), QUEUE_NAME, THREAD_NUM, executeMysql);
    pool.start();

    server.start();
}

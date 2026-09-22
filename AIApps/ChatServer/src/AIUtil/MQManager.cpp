#include"../include/AIUtil/MQManager.h"
#include"../include/AIUtil/EnvUtil.h"

namespace {
AmqpClient::Channel::ptr_t openChannel(const std::string& host, int port,
                                       const std::string& user,
                                       const std::string& password,
                                       const std::string& vhost = "/") {
    AmqpClient::Channel::OpenOpts opts;
    opts.host = host;
    opts.port = port;
    opts.vhost = vhost;
    opts.auth = AmqpClient::Channel::OpenOpts::BasicAuth(user, password);
    return AmqpClient::Channel::Open(opts);
}
}

// ------------------- MQManager -------------------
MQManager::MQManager(size_t poolSize)
    : poolSize_(poolSize), counter_(0) {
    const std::string host = envOr("RABBITMQ_HOST", "localhost");
    const int port = envOrInt("RABBITMQ_PORT", 5672);
    const std::string user = envOr("RABBITMQ_USER", "guest");
    const std::string password = envOr("RABBITMQ_PASSWORD", "guest");
    for (size_t i = 0; i < poolSize_; ++i) {
        auto conn = std::make_shared<MQConn>();
        conn->channel = openChannel(host, port, user, password);

        pool_.push_back(conn);
    }
}

void MQManager::publish(const std::string& queue, const std::string& msg) {
    // fetch_add 是原子 +1 操作，保证线程安全
    size_t index = counter_.fetch_add(1) % poolSize_;
    auto& conn = pool_[index];

    std::lock_guard<std::mutex> lock(conn->mtx);
    auto message = AmqpClient::BasicMessage::Create(msg);
    conn->channel->BasicPublish("", queue, message);
}

// ------------------- RabbitMQThreadPool -------------------

void RabbitMQThreadPool::start() {
    for (int i = 0; i < thread_num_; ++i) {
        workers_.emplace_back(&RabbitMQThreadPool::worker, this, i);
    }
}

void RabbitMQThreadPool::shutdown() {
    stop_ = true;
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
}

void RabbitMQThreadPool::worker(int id) {
    try {
        // Each thread has its own independent channel
        const int port = envOrInt("RABBITMQ_PORT", 5672);
        const std::string user = envOr("RABBITMQ_USER", "guest");
        const std::string password = envOr("RABBITMQ_PASSWORD", "guest");
        auto channel = openChannel(rabbitmq_host_, port, user, password);
        // set exclusive
        channel->DeclareQueue(queue_name_, false, true, false, false);
        // Prevent channel error: 403: AMQP_BASIC_CONSUME_METHOD caused: ACCESS_REFUSED - queue 
        // 'sql_queue' in vhost '/' in exclusive use
        // std::string consumer_tag = channel->BasicConsume(queue_name_, "");
        std::string consumer_tag = channel->BasicConsume(queue_name_, "", true, false, false);

        channel->BasicQos(consumer_tag, 1); 
        
        // shutdown 会把 stop_ 设置为 true，所以这里会退出循环
        while (!stop_) {
            AmqpClient::Envelope::ptr_t env;
            bool ok = channel->BasicConsumeMessage(consumer_tag, env, 500); // 500ms 
            if (ok && env) {
                std::string msg = env->Message()->Body();
                handler_(msg);          
                channel->BasicAck(env); 
            }
        }

        channel->BasicCancel(consumer_tag);
    }
    catch (const std::exception& e) {
        std::cerr << "Thread " << id << " exception: " << e.what() << std::endl;
    }
}

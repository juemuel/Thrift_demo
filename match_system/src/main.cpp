// This autogenerated skeleton file illustrates how to build a server.
// You should copy it to another filename to avoid overwriting it.

// 引入基于match.thrift的服务端所需的Match.h,对应的客户端可与之通讯
#include "match_server/Match.h"
// 引入基于save.thrift的客户端服务所需的Save.h,和thrift的系统头文件
#include "save_client/Save.h"
#include <thrift/transport/TTransportUtils.h>
#include <thrift/transport/TSocket.h>

// thrift一般服务器头文件
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TSimpleServer.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TBufferTransports.h>
// thrift多线程服务器头文件
#include <thrift/concurrency/ThreadManager.h>
#include <thrift/concurrency/ThreadFactory.h>
#include <thrift/server/TThreadedServer.h>
// thrift线程池服务器(待补充)
// #include <thrift/server/TThreadPoolServer.h>

#include <iostream>
#include "thread"
#include "mutex"
#include "condition_variable"
#include "queue"
#include "vector"
#include "unistd.h"

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::server;

using namespace  ::match_service;
using namespace ::save_service; // 引入源自save.thrift的命名空间
using namespace std; // 多人开发不要这样写



struct Task{
    User user;
    string type;
};

// 定义消息队列结构体：消息的队列、锁和条件变量
struct MessageQueue{
    queue<Task> q;
    mutex m;
    condition_variable cv;
}message_queue;

class Poll{
    public:
        void add(User user){
            users.push_back(user);
            wt.push_back(0);
        }

        void remove(User user){
            for(uint32_t i = 0; i < users.size(); i++){
                if(users[i].id == user.id){
                    users.erase(users.begin() + i);
                    wt.erase(wt.begin() + i);
                    break;
                }
            }
        }


    bool check_match(uint32_t i, uint32_t j) {
        auto a = users[i], b = users[j];

        int dt = abs(a.score - b.score);
        int a_max_dif = wt[i] * 50;
        int b_max_dif = wt[j] * 50;

        return dt <= a_max_dif && dt <= b_max_dif;
    }

    void match() {
        for (uint32_t i = 0; i < wt.size(); i++)
            wt[i]++;   // 等待秒数 + 1
        while (users.size() > 1) {
            bool flag = true;
            for (uint32_t i = 0; i < users.size(); i++) {
                for (uint32_t j = i + 1; j < users.size(); j++) {
                    if (check_match(i, j)) {
                        auto a = users[i], b = users[j];
                        users.erase(users.begin() + j);
                        users.erase(users.begin() + i);
                        wt.erase(wt.begin() + j);
                        wt.erase(wt.begin() + i);
                        save_result(a.id, b.id);
                        flag = false;
                        break;
                    }
                }

                if (!flag) break;
            }

            if (flag) break;
        }
    }

        void save_result(int a, int b){
            printf("%d 和 %d匹配成功\n", a, b);
            // 保存在server端，因此用server端的ip
            std::shared_ptr<TTransport> socket(new TSocket("123.57.67.128", 9090));
            std::shared_ptr<TTransport> transport(new TBufferedTransport(socket));
            std::shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport));
            SaveClient client(protocol); // 此处修改为Save
            try {
                transport->open();
                // 2.补充中间部分

                // 主义,通过 server 用户名和md5加密后的密码，才能把数据保存到 myserver 的 result.txt ，再加上两个用户
                int res = client.save_data("acs_9080", "c3945d7e", a, b);
                if (!res) puts("save success");
                else puts("save failed");

                transport->close();
            } catch (TException& tx) {
                cout << "ERROR: " << tx.what() << endl;
            }


        }

    private:
        vector<User> users;
        vector<int> wt; // wait time,等待时间
}pool;


class MatchHandler : virtual public MatchIf {
    public:
        MatchHandler() {
            // Your initialization goes here
        }

        /**
         * user: 添加的用户信息
         * info: 附加信息
         * 在匹配池中添加一个名用户
         * 
         * @param user
         * @param info
         */
        int32_t add_user(const User& user, const std::string& info) {
            // Your implementation goes here
            printf("add_user\n");

            unique_lock<mutex> lck(message_queue.m);
            message_queue.q.push({user, "add"});
            message_queue.cv.notify_all();

            return 0;
        }

        /**
         * user: 删除的用户信息
         * info: 附加信息
         * 从匹配池中删除一名用户
         * 
         * @param user
         * @param info
         */
        int32_t remove_user(const User& user, const std::string& info) {
            // Your implementation goes here
            printf("remove_user\n");

            unique_lock<mutex> lck(message_queue.m);
            message_queue.q.push({user, "remove"});
            message_queue.cv.notify_all();// notify_one也可以，因为这里只有一个；remove也是task操作；


            return 0;
        }

};





void consume_task(){
    while(true){
        unique_lock<mutex> lck(message_queue.m); // 上锁
        if(message_queue.q.empty()){
            // #1.考虑到上锁判空可能死循环，可以将消息队列阻塞
            // message_queue.cv.wait(lck);
            // #2.每1s执行一次,用时间控制,那么可以取消其他地方的match
            lck.unlock();
            pool.match();
            sleep(1);
        }
        else{
            auto task = message_queue.q.front();
            message_queue.q.pop();
            lck.unlock(); // 解锁
            // do task
            if(task.type == "add") pool.add(task.user);
            else if(task.type == "remove") pool.remove(task.user);

            // 采取#2用时间控制匹配,可以在这里去掉匹配
            //pool.match(); // 匹配
        }

    }
}

// 一个工厂模式方便多线程服务器获取连接状态
class MatchCloneFactory : virtual public MatchIfFactory {
    public:
        ~MatchCloneFactory() override = default;
        MatchIf* getHandler(const ::apache::thrift::TConnectionInfo& connInfo) override
        {
            std::shared_ptr<TSocket> sock = std::dynamic_pointer_cast<TSocket>(connInfo.transport);
            // cout << "Incoming connection\n";
            // cout << "\tSocketInfo: "  << sock->getSocketInfo() << "\n";
            // cout << "\tPeerHost: "    << sock->getPeerHost() << "\n";
            // cout << "\tPeerAddress: " << sock->getPeerAddress() << "\n";
            // cout << "\tPeerPort: "    << sock->getPeerPort() << "\n";
            return new MatchHandler;
        }
        void releaseHandler( MatchIf* handler) override {
            delete handler;
        }
};

int main(int argc, char **argv) {
    int port = 9090;
    // 这边的部分使用哪一种服务器,就写哪一种
    TThreadedServer server(
            std::make_shared<MatchProcessorFactory>(std::make_shared<MatchCloneFactory>()),
            std::make_shared<TServerSocket>(9090), //port
            std::make_shared<TBufferedTransportFactory>(),
            std::make_shared<TBinaryProtocolFactory>());

    cout << "Start Match Server" << endl;
    // 给消费者单开一个队列
    thread matching_thread(consume_task);

    server.serve();

    return 0;
}


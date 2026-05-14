#include"rpcserver.h"   

void MainReactor::listener_callback(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *addr, int socklen, void *arg)
{   

    static int next_worker = 0;
    MainReactor* main_reactor = static_cast<MainReactor*>(arg);
    std::vector<std::unique_ptr<WorkerReactor>>& worker_reactors = main_reactor->worker_reactors;
    // 将新连接分发给工作线程
    // ..

    // 轮询分配
    worker_reactors[next_worker]->AddConnection(fd);
    next_worker = (next_worker + 1) % main_reactor->worker_threads_;
}
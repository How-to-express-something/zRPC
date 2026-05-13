#include"rpcserver.h"   

void MainReactor::listener_callback(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *addr, int socklen, void *arg)
{   

    static int next_worker = 0;
    std::vector<WorkerReactor>* worker_reactors = static_cast<std::vector<WorkerReactor>*>(arg);
    // 将新连接分发给工作线程
    // ..

    // 轮询分配
    (*worker_reactors)[next_worker].AddConnection(fd);
    next_worker = (next_worker + 1) % worker_threads_;
}
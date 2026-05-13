#pragma once

#include <functional>
#include <google/protobuf/message.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/thread.h>
#include <map>
#include "protocol.h"
#include "thread_pool.h"
#include <mutex>
#include <queue>
#include <unistd.h>

using Handler = std::function<bool(const RpcRequest&,const RpcResponse&,std::string error)>;

class WorkerReactor
{   
public:

    WorkerReactor(struct event_base* base, int id):base(base),id_(id)
    {
        pipe(notify_pipe);
        notify_event = event_new(base, notify_pipe[0], EV_READ | EV_PERSIST, WorkerReactor::notify_callback, this);
        event_add(notify_event, nullptr);
    }

    void AddConnection(evutil_socket_t fd)
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        pending_fd.push(fd);

        char c = 1;
        write(notify_pipe[1], &c, 1);
    }

    void Start()
    {
        worker_thread([this]() {
            event_base_dispatch(base);
        }).detach();
    }


    void stop()
    {
        event_base_loopexit(base, nullptr);

        if(worker_thread.joinable())
        {
            worker_thread.join();
        }

    }

private:
    void notify_callback(evutil_socket_t fd, short events, void* arg)
    {
        WorkerReactor* reactor = static_cast<WorkerReactor*>(arg);
        char buf[1];
        read(fd, buf, 1);

        std::lock_guard<std::mutex> lock(reactor->queue_mutex);
        while (!reactor->pending_fd.empty())
        {
            evutil_socket_t client_fd = reactor->pending_fd.front();
            reactor->pending_fd.pop();
            // 这里可以为client_fd创建bufferevent并设置回调
            // ...
        }
    }

   

    struct event_base* base;
    std::thread worker_thread;
    std::mutex;
    std::queue<evutil_socket_t>  pending_fd;
    int notify_pipe[2];

}


class MainReactor
{
public:
    MainReactor(short port,const std::string& ip = "127.0.0.1",int workers_num = 8):port_(port),ip_(ip),worker_threads_(workers_num){

        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_port = htons(port); 
        inet_pton(AF_INET, ip_.c_str(), &sin.sin_addr);
    
        base = event_base_new();
    
        listener = evconnlistener_new_bind(
            base, listener_callback, &worker_reactors,
            LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE,
            10, (struct sockaddr*)&sin, sizeof(sin));
    
        if (!listener) {
            event_base_free(base);
            return;
        }

    };

    ~MainReactor()
    {
        if(listener) evconnlistener_free(listener);
        if(base) event_base_free(base);
    }

    void start()
    {   
        is_running = true;
        event_base_dispatch(base);


        for(int i = 0; i < worker_threads_; i++)
        {
            struct event_base* worker_base = event_base_new();
            worker_bases.push_back(worker_base);
            WorkerReactor reactor(worker_base, i);
            worker_reactors.push_back(std::move(reactor));
            worker_reactors.back().Start();
        }


    }
    void stop()
    {
        event_base_loopexit(base, nullptr);

        for(int i = 0; i < worker_threads_; i++)
        {
            worker_reactors[i].stop();
        }

        for(int i = 0; i < worker_threads_; i++)
        {
            if(worker_bases[i])
            {
                event_base_free(worker_bases[i]);
            }
        }

    }


private:
    short port_;
    std::string ip_;    
    void listener_callback(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *addr, int socklen, void *arg);
    struct event_base *base;
    struct evconnlistener *listener;
    std::vector<WorkerReactor> worker_reactors;
    std::vector<struct event_base*> worker_bases;
    bool is_running = false;
};



class RPCServer
{
public:
    RPCServer(short port,const std::string& ip = "127.0.0.1"):main_reactor(new MainReactor(port,ip)),thread_pool(new ThreadPool(8)){};
    ~RPCServer();
    
    void register_handler(const std::string& service, const std::string& method, Handler handler)
    {
        std::string key = splice_key(service, method);
        handlers_[key] = std::move(handler);
    }

    template<typename RequestType, typename ResponseType>
    void register_typedhandler(const RequestType& req, const ResponseType& resp, const std::string& service,const std::string& method,std::function<bool(const RequestType&, const ResponseType&, std::string error)> handler)
    {
        static_assert(std::is_base_of<google::protobuf::Message, RequestType>::value, "RequestType must be a protobuf message");
        static_assert(std::is_base_of<google::protobuf::Message, ResponseType>::value, "ResponseType must be a protobuf message");


        //处理完RequestType ResponseType后，调用handler，并将结果转换为RpcRequest和RpcResponse传递给通用handler
        Handler wrapper = [handler](const RpcRequest& rpc_req, const RpcResponse& rpc_resp, std::string error) -> bool {

            //处理

        }

        register_handler(service, method, wrapper);
    }

    void start();
    void stop();



private:
    std::string splice_key(const std::string& service, const std::string& method) {
        return service + "#" + method;
    }

    std::map<std::string, Handler> handlers_;
    MainReactor* main_reactor;
    ThreadPool* thread_pool;
};
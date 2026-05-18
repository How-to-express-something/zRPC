#pragma once

/*
RpcSession生命周期还未处理

*/

#include <arpa/inet.h>
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
#include <memory>
#include <iostream>
#include "coder.h"


using Handler = std::function<bool(const RpcRequest& request, RpcResponse& response,std::string* error)>;



class SafeHandlerMap
{   
public:
        void Insert(const std::string& key, Handler handler)
        {
            std::lock_guard<std::mutex> lock(map_mutex);
            handler_map[key] = handler;
        }

        bool Get(const std::string& key, Handler& handler)
        {
            std::lock_guard<std::mutex> lock(map_mutex);
            auto it = handler_map.find(key);
            if(it != handler_map.end())
            {
                handler = it->second;
                return true;
            }
            return false;
        }
private:
    std::mutex map_mutex;
    std::map<std::string, Handler> handler_map;
};



//负责解析请求，submit任务到线程池
class WorkerReactor
{   

    class RpcSession : public std::enable_shared_from_this<RpcSession> {
        friend class WorkerReactor;
        public:
            RpcSession(struct bufferevent* bev) : bev_(bev) {}

    // 可以从任意线程调用，将响应数据投递到事件循环发送
         void SendResponse(const std::string& serialized_response) {
        // 把数据拷贝到发送队列，并激活事件循环中的发送操作
         {
            std::lock_guard<std::mutex> lock(mutex_);
            send_queue_.push(serialized_response);
        }
        // 通知事件循环有数据可写（例如激活 bufferevent 的写事件，或使用 event_active）
        // 注意：bufferevent_write 通常只能在事件循环线程调用，这里我们用 event 来触发
        event_active(send_ev_, EV_WRITE, 0);
        }

    // 事件循环线程回调，实际执行发送
    static void SendEventCallback(evutil_socket_t, short, void* arg) {
        auto* self = static_cast<RpcSession*>(arg);
        std::string data;
        {
            std::lock_guard<std::mutex> lock(self->mutex_);
            if (!self->send_queue_.empty()) {
                data = std::move(self->send_queue_.front());
                self->send_queue_.pop();
            }
        }
        if (!data.empty()) {
            bufferevent_write(self->bev_, data.data(), data.size());
        }
    }

private:
    bufferevent* bev_;
    event* send_ev_;  // 在事件循环线程初始化
    std::mutex mutex_;
    std::queue<std::string> send_queue_;
};

private:
    int id_;
    struct event_base* base;
    std::thread worker_thread;
    std::mutex mutex;
    std::queue<evutil_socket_t>  pending_fd;
    int notify_pipe[2];
    static ThreadPool* thread_pool; //
    static SafeHandlerMap* handlers_;
    event* notify_event;

public:

    WorkerReactor(struct event_base* base, int id):base(base),id_(id)
    {
        pipe(notify_pipe);
        notify_event = event_new(base, notify_pipe[0], EV_READ | EV_PERSIST, notify_callback, this);
        event_add(notify_event, nullptr);
       std::cout << "WorkerReactor " << id_ << " initialized." << std::endl;
    }

    static void set_handlers(SafeHandlerMap* handlers)
    {
        handlers_ = handlers;
    }

    static void set_thread_pool(ThreadPool* pool)
    {
        thread_pool = pool;
    }

    void AddConnection(evutil_socket_t fd)
    {
        std::lock_guard<std::mutex> lock(mutex);
        pending_fd.push(fd);

        char c = 1;
        write(notify_pipe[1], &c, 1);
    }

    void Start()
    {
        worker_thread = std::thread([this]() {
        event_base_dispatch(base);
    });
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
    static void notify_callback(evutil_socket_t fd, short events, void* arg)
    {
        WorkerReactor* reactor = static_cast<WorkerReactor*>(arg);
        char buf[1];
        read(fd, buf, 1);

        reactor->HandlePendingConnections();
    }

    void HandlePendingConnections()
    {
        std::lock_guard<std::mutex> lock(mutex);
        while (!pending_fd.empty())
        {
            evutil_socket_t fd = pending_fd.front();
            pending_fd.pop();
            CreateBufferevent(fd);
        }
    }


    void CreateBufferevent(evutil_socket_t fd) {
        // 注意：这里用的是 worker 自己的 base_
        struct bufferevent* bev = bufferevent_socket_new(
            base, fd, BEV_OPT_CLOSE_ON_FREE);
        
        if (!bev) {
            close(fd);
            return;
        }


        RpcSession* session = new RpcSession(bev);
        session->send_ev_ = event_new(base, -1, EV_WRITE | EV_PERSIST, RpcSession::SendEventCallback, session);
        event_add(session->send_ev_, nullptr);
        bufferevent_setcb(bev, 
                          server_read_callback,   // 你的读回调
                          nullptr,                // 写回调
                          server_event_callback,  // 你的事件回调
                          session);               // 可传自定义上下文
        bufferevent_enable(bev, EV_READ | EV_WRITE);


       
    }
   

    static void server_read_callback(struct bufferevent* bev, void* ctx)
    {   
        struct FrameHeader header;
        struct evbuffer* input = bufferevent_get_input(bev);

        RpcSession* session = static_cast<RpcSession*>(ctx);
        if(evbuffer_get_length(input) >= FrameHeader::header_size)
        {
            char header_buf[FrameHeader::header_size];
            evbuffer_copyout(input, header_buf, FrameHeader::header_size);
            if(!DecodeHeader(header_buf, header))
            {
                // 解析失败，关闭连接
                bufferevent_free(bev);
                return;
            }

            // 这里可以根据 header.body_length 判断是否有完整的请求体数据
            if(evbuffer_get_length(input) >= FrameHeader::header_size + header.body_length)
            {
                // 
                evbuffer_drain(input, FrameHeader::header_size); // 移除已解析的头部数据
                std::string body_buf;
                body_buf.resize(header.body_length);
                evbuffer_remove(input, body_buf.data(), header.body_length);
                

                // 解析请求体并提交任务到线程池
                // ...
                std::string* error = new std::string;
                DecodedFrame decoded;
                if(!VerifyAndDecodeFrame(header, body_buf, decoded, CodecOptions(), error))
                {
                    // 解析失败，关闭连接
                    std::cout << "Failed to decode frame: " << *error << std::endl;
                    delete error;
                    bufferevent_free(bev);
                    return;
                }
                else
                {   
                    if(decoded.type == MessageType::Heartbeat)
                    {
                        // 处理心跳，可以直接回复一个心跳响应
                        RpcResponse heartbeat_response;
                        heartbeat_response.request_id = decoded.request.request_id;
                        heartbeat_response.status_code = 0; // 成功
                        std::string frame;
                        CodecOptions options;
                        if(EncodeResponse(heartbeat_response, options, frame, error))
                        {
                            session->SendResponse(frame);
                        }
                        else
                        {
                            std::cout << "Failed to encode heartbeat response: " << *error << std::endl;
                            delete error;
                            bufferevent_free(bev);
                            return;
                        }
                    }
                    else
                    {
                        std::string key = decoded.request.service + "#" + decoded.request.method;
                        Handler handler;
                        if(!handlers_->Get(key, handler))
                        {
                            // 没有找到对应的处理函数，可以构造一个错误响应并发送回客户端
                            RpcResponse error_response;
                            error_response.request_id = decoded.request.request_id;
                            error_response.status_code = -1; // 错误
                            error_response.error_message = "No handler found for " + key;
                            std::string frame;
                            CodecOptions options;
                            if(EncodeResponse(error_response, options, frame, error))
                            {
                                session->SendResponse(frame);
                                return;
                            }
                            else
                            {
                                std::cout << "Failed to encode error response: " << *error << std::endl;
                                delete error;
                                bufferevent_free(bev);
                                return;
                            }
                        }

                        RpcResponse* response = new RpcResponse; // 你需要根据实际情况构造响应对象
                         thread_pool->submit([response,handler,session,error,decoded]()->bool{
                                handler(decoded.request, *response, error);
                                std::string frame;
                                CodecOptions options;
                                if(EncodeResponse(*response, options, frame, error))
                                {                                   
                                     session->SendResponse(frame);
                                }
                                else
                                {
                                    std::cout << "Failed to encode response: " << *error << std::endl;
                                    delete error;
                                    return false;
                                }
                                delete response;
                                delete error;
                        return true;
                    });
                    }
                    
                }
            }
        }

        return;

            // 这里是处理读事件的回调函数
            // 你可以从 bev 中读取数据，解析请求，并提交任务到线程池
    }


    static void server_event_callback(struct bufferevent *bev, short events, void *ctx) {
        if (events & BEV_EVENT_ERROR) {
            
        }
        if (events & BEV_EVENT_EOF) {
         
        }
         if (events & BEV_EVENT_CONNECTED) {
      
        }
    
        if (events & (BEV_EVENT_ERROR | BEV_EVENT_EOF)) {
        bufferevent_free(bev);
        }
    }


};


class MainReactor
{
private:
    int worker_threads_;
    short port_;
    std::string ip_;    
    static void listener_callback(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *addr, int socklen, void *arg);
    struct event_base *base;
    struct evconnlistener *listener;
    std::vector<std::unique_ptr<WorkerReactor>> worker_reactors;
    std::vector<struct event_base*> worker_bases;
    bool is_running = false;

public:
    MainReactor(short port,ThreadPool* pool,SafeHandlerMap* handlers,const std::string& ip = "127.0.0.1",int workers_num = 8):port_(port),ip_(ip),worker_threads_(workers_num){

        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_port = htons(port); 
        inet_pton(AF_INET, ip_.c_str(), &sin.sin_addr);
    
        base = event_base_new();
    
        listener = evconnlistener_new_bind(
            base, listener_callback, this,
            LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE,
            10, (struct sockaddr*)&sin, sizeof(sin));
    
        if (!listener) {
            
            event_base_free(base);
            std::cerr << "Listener failed: " << strerror(errno) << std::endl;
            base = nullptr;
            return;
        }
        
        std::cout << "MainReactor initialized on " << ip_ << ":" << port_ << " with " << worker_threads_ << " worker threads." << std::endl;

        WorkerReactor::set_thread_pool(pool);
        WorkerReactor::set_handlers(handlers);

         for(int i = 0; i < worker_threads_; i++)
        {
            struct event_base* worker_base = event_base_new();
            worker_bases.push_back(worker_base);
            worker_reactors.push_back(std::make_unique<WorkerReactor>(worker_base, i));
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
        


        for(int i = 0; i < worker_threads_; i++)
        {
            worker_reactors[i]->Start();
        }

        event_base_dispatch(base);

    }
    void stop()
    {
        event_base_loopexit(base, nullptr);

        for(int i = 0; i < worker_threads_; i++)
        {
            worker_reactors[i]->stop();
        }

        for(int i = 0; i < worker_threads_; i++)
        {
            if(worker_bases[i])
            {
                event_base_free(worker_bases[i]);
            }
        }

    }


    


};



class RPCServer
{
public:
    RPCServer(short port,const std::string& ip = "127.0.0.1"):thread_pool(new ThreadPool(8)),main_reactor(new MainReactor(port,thread_pool,&handlers,ip,8)){
    };
    ~RPCServer()
    {
        if(is_running)
        {
            stop();
        }

        delete main_reactor;
        delete thread_pool;

    }
    
    void register_handler(const std::string& service, const std::string& method, Handler handler)
    {
        std::string key = splice_key(service, method);
        handlers.Insert(key, handler);
    }

    template<typename RequestType, typename ResponseType>
    void register_typedhandler(const RequestType& req, const ResponseType& resp, const std::string& service,const std::string& method,std::function<bool(const RequestType&, const ResponseType&, std::string error)> handler)
    {
        static_assert(std::is_base_of<google::protobuf::Message, RequestType>::value, "RequestType must be a protobuf message");
        static_assert(std::is_base_of<google::protobuf::Message, ResponseType>::value, "ResponseType must be a protobuf message");


        //处理完RequestType ResponseType后，调用handler，并将结果转换为RpcRequest和RpcResponse传递给通用handler
        Handler wrapper = [handler](const RpcRequest& rpc_req, const RpcResponse& rpc_resp, std::string error) -> bool {

            //处理
            return true;
        };

        register_handler(service, method, wrapper);
    }

    void start()
    {   
        thread_pool->init();
        main_reactor->start();
        is_running = true;
    };
    void stop()
    {   
        is_running = false;
        main_reactor->stop();
        thread_pool->shutdown();
        
    }



private:
    std::string splice_key(const std::string& service, const std::string& method) {
        return service + "#" + method;
    }

    SafeHandlerMap handlers;
    ThreadPool* thread_pool;
    MainReactor* main_reactor;
    std::mutex handlers_mutex;
    bool is_running = false;
};
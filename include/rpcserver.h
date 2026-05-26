#pragma once

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
#include <chrono>
#include "coder.h"
#include "serialzation.h"
#include "log.h"
#include "metrics.h"


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
            RpcSession(struct bufferevent* bev, ThreadPool* pool, SafeHandlerMap* handlers)
                : thread_pool_(pool), handlers_(handlers), bev_(bev) {}

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

    ThreadPool* thread_pool_;
    SafeHandlerMap* handlers_;

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
    ThreadPool* thread_pool_ = nullptr;
    SafeHandlerMap* handlers_ = nullptr;
    event* notify_event;

public:

    WorkerReactor(struct event_base* base, ThreadPool* pool, SafeHandlerMap* handlers, int id)
        : id_(id), base(base), thread_pool_(pool), handlers_(handlers)
    {
        pipe(notify_pipe);
        notify_event = event_new(base, notify_pipe[0], EV_READ | EV_PERSIST, notify_callback, this);
        event_add(notify_event, nullptr);
       LOG_INFO("WorkerReactor %d initialized.", id_);
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
    static void notify_callback(evutil_socket_t fd, short /*events*/, void* arg)
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


        RpcSession* session = new RpcSession(bev, thread_pool_, handlers_);
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
            if(!DecodeHeader(std::string(header_buf, FrameHeader::header_size), header))
            {
                bufferevent_free(bev);
                return;
            }

            if(evbuffer_get_length(input) >= FrameHeader::header_size + header.body_length)
            {
                evbuffer_drain(input, FrameHeader::header_size);
                std::string body_buf;
                body_buf.resize(header.body_length);
                evbuffer_remove(input, body_buf.data(), header.body_length);

                auto error = std::make_shared<std::string>();
                DecodedFrame decoded;
                if(!VerifyAndDecodeFrame(header, body_buf, decoded, CodecOptions(), error.get()))
                {
                    LOG_ERROR("Failed to decode frame: %s", error->c_str());
                    bufferevent_free(bev);
                    return;
                }
                else
                {
                    if(decoded.type == MessageType::Heartbeat)
                    {
                        RpcResponse heartbeat_response;
                        heartbeat_response.request_id = decoded.request.request_id;
                        heartbeat_response.status_code = 0;
                        std::string frame;
                        CodecOptions options;
                        if(EncodeResponse(heartbeat_response, options, frame, error.get()))
                        {
                            session->SendResponse(frame);
                        }
                        else
                        {
                            LOG_ERROR("Failed to encode heartbeat response: %s", error->c_str());
                            bufferevent_free(bev);
                            return;
                        }
                    }
                    else
                    {
                        std::string key = decoded.request.service + "#" + decoded.request.method;
                        Handler handler;
                        if(!session->handlers_->Get(key, handler))
                        {
                            RpcResponse error_response;
                            error_response.request_id = decoded.request.request_id;
                            error_response.status_code = -1;
                            error_response.error_message = "No handler found for " + key;
                            std::string frame;
                            CodecOptions options;
                            if(EncodeResponse(error_response, options, frame, error.get()))
                            {
                                session->SendResponse(frame);
                                return;
                            }
                            else
                            {
                                LOG_ERROR("Failed to encode error response: %s", error->c_str());
                                bufferevent_free(bev);
                                return;
                            }
                        }

                        auto response = std::make_shared<RpcResponse>();
                        auto frame_received_time = std::chrono::steady_clock::now();
                         session->thread_pool_->submit([response, handler, session, error, decoded, frame_received_time]()->bool{
                                handler(decoded.request, *response, error.get());

                                auto handler_done = std::chrono::steady_clock::now();
                                int64_t latency_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                    handler_done - frame_received_time).count();
                                MetricsCollector::Instance().RecordServerLatencyUs(latency_us);

                                std::string frame;
                                CodecOptions options;
                                if(EncodeResponse(*response, options, frame, error.get()))
                                {
                                     session->SendResponse(frame);
                                }
                                else
                                {
                                    LOG_ERROR("Failed to encode response: %s", error->c_str());
                                    return false;
                                }
                        return true;
                    });
                    }

                }
            }
        }

        return;
    }


    static void server_event_callback(struct bufferevent *bev, short events, void *ctx) {
        RpcSession* session = static_cast<RpcSession*>(ctx);
        if (events & (BEV_EVENT_ERROR | BEV_EVENT_EOF)) {
            if (session) {
                if (session->send_ev_) {
                    event_free(session->send_ev_);
                    session->send_ev_ = nullptr;
                }
                delete session;
            }
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
    MainReactor(short port,ThreadPool* pool,SafeHandlerMap* handlers,const std::string& ip = "127.0.0.1",int workers_num = 8):worker_threads_(workers_num), port_(port), ip_(ip) {

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
            LOG_ERROR("Listener failed: %s", strerror(errno));
            base = nullptr;
            return;
        }
        
        LOG_INFO("MainReactor initialized on %s:%d with %d worker threads.", ip_.c_str(), port_, worker_threads_);

         for(int i = 0; i < worker_threads_; i++)
        {
            struct event_base* worker_base = event_base_new();
            worker_bases.push_back(worker_base);
            worker_reactors.push_back(std::make_unique<WorkerReactor>(worker_base, pool, handlers, i));
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
    RPCServer(short port, const std::string& ip = "127.0.0.1", int workers_num = 4)
        : thread_pool(new ThreadPool(workers_num))
        , main_reactor(new MainReactor(port, thread_pool, &handlers, ip, workers_num)) {
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
    void register_typedhandler(const std::string& service, const std::string& method,
        std::function<bool(const RequestType&, ResponseType&, std::string* error)> handler)
    {
        static_assert(std::is_base_of<google::protobuf::Message, RequestType>::value,
                      "RequestType must be a protobuf message");
        static_assert(std::is_base_of<google::protobuf::Message, ResponseType>::value,
                      "ResponseType must be a protobuf message");

        Handler wrapper = [handler](const RpcRequest& rpc_req, RpcResponse& rpc_resp,
                                     std::string* error) -> bool {
            RequestType req;
            ResponseType resp;

            if (!DeserializeMessage(rpc_req.payload, rpc_req.serialization, req, error)) {
                rpc_resp.status_code = -1;
                rpc_resp.error_message = "deserialize failed: " + *error;
                return false;
            }

            if (!handler(req, resp, error)) {
                if (rpc_resp.status_code == 0) rpc_resp.status_code = -1;
                return false;
            }

            if (!SerializeMessage(resp, rpc_req.serialization, rpc_resp.payload, error)) {
                rpc_resp.status_code = -1;
                rpc_resp.error_message = "serialize failed: " + *error;
                return false;
            }

            rpc_resp.request_id = rpc_req.request_id;
            rpc_resp.status_code = 0;
            rpc_resp.serialization = rpc_req.serialization;
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
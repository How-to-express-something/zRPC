#pragma once

#include <arpa/inet.h>
#include <atomic>
#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <functional>
#include <google/protobuf/message.h>
#include "protocol.h"
#include "coder.h"
#include "serialzation.h"

class RpcClient {
public:
    RpcClient();
    ~RpcClient();

    RpcClient(const RpcClient&) = delete;
    RpcClient& operator=(const RpcClient&) = delete;

    bool Connect(const std::string& ip, short port);
    void Disconnect();
    bool IsConnected() const { return connected_.load(); }

    // Synchronous call with raw payload
    RpcResponse Call(const RpcRequest& request, int timeout_ms = 3000);

    // Convenience: build request from fields (raw payload)
    RpcResponse Call(const std::string& service, const std::string& method,
                     const std::string& payload, int timeout_ms = 3000);

    // Typed call: serialize/deserialize protobuf messages automatically
    template<typename RequestType, typename ResponseType>
    RpcResponse Call(const std::string& service, const std::string& method,
                     const RequestType& req, ResponseType& resp,
                     SerializationType ser_type = SerializationType::Protobuf,
                     int timeout_ms = 3000)
    {
        static_assert(std::is_base_of<google::protobuf::Message, RequestType>::value,
                      "RequestType must be a protobuf message");
        static_assert(std::is_base_of<google::protobuf::Message, ResponseType>::value,
                      "ResponseType must be a protobuf message");

        std::string payload;
        std::string error;
        if (!SerializeMessage(req, ser_type, payload, &error)) {
            RpcResponse err_resp;
            err_resp.status_code = -1;
            err_resp.error_message = "client serialize failed: " + error;
            return err_resp;
        }

        RpcRequest rpc_req;
        rpc_req.request_id = next_request_id_.fetch_add(1);
        rpc_req.service = service;
        rpc_req.method = method;
        rpc_req.payload = payload;
        rpc_req.serialization = ser_type;
        rpc_req.timeout_ms = timeout_ms;

        auto rpc_resp = Call(rpc_req, timeout_ms);

        if (rpc_resp.status_code == 0) {
            if (!DeserializeMessage(rpc_resp.payload, ser_type, resp, &error)) {
                rpc_resp.status_code = -1;
                rpc_resp.error_message = "client deserialize failed: " + error;
            }
        }

        return rpc_resp;
    }

    // Async call: returns future immediately
    std::future<RpcResponse> CallAsync(const RpcRequest& request);

private:
    struct PendingRequest {
        std::promise<RpcResponse> promise;
        std::future<RpcResponse> future;
    };

    void StartEventLoop();
    void StopEventLoop();

    void EnqueueWrite(const std::string& data);

    static void read_callback(struct bufferevent* bev, void* ctx);
    static void event_callback(struct bufferevent* bev, short events, void* ctx);
    static void send_event_callback(evutil_socket_t fd, short events, void* arg);

    struct event_base* base_;
    struct bufferevent* bev_;
    struct event* send_ev_;
    std::thread event_thread_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};

    std::mutex pending_mutex_;
    std::map<uint64_t, PendingRequest> pending_;

    std::mutex send_mutex_;
    std::queue<std::string> send_queue_;

    std::atomic<uint64_t> next_request_id_{1};

    CodecOptions codec_options_;
};

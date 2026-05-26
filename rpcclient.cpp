#include "rpcclient.h"
#include <event2/thread.h>
#include <chrono>
#include "log.h"
#include "metrics.h"

RpcClient::RpcClient()
{
    evthread_use_pthreads();
    base_ = event_base_new();
}

RpcClient::~RpcClient()
{
    Disconnect();
    if (base_) {
        event_base_free(base_);
        base_ = nullptr;
    }
}

bool RpcClient::Connect(const std::string& ip, short port)
{
    if (connected_.load()) {
        Disconnect();
    }

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &sin.sin_addr) <= 0) {
        LOG_ERROR("RpcClient: invalid address %s", ip.c_str());
        return false;
    }

    bev_ = bufferevent_socket_new(base_, -1, BEV_OPT_CLOSE_ON_FREE);
    if (!bev_) {
        LOG_ERROR("RpcClient: failed to create bufferevent");
        return false;
    }

    if (bufferevent_socket_connect(bev_, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        LOG_ERROR("RpcClient: connect failed to %s:%d", ip.c_str(), port);
        bufferevent_free(bev_);
        bev_ = nullptr;
        return false;
    }

    send_ev_ = event_new(base_, -1, EV_WRITE | EV_PERSIST, send_event_callback, this);
    event_add(send_ev_, nullptr);

    bufferevent_setcb(bev_, read_callback, nullptr, event_callback, this);
    bufferevent_enable(bev_, EV_READ | EV_WRITE);

    connected_.store(true);
    running_.store(true);
    event_thread_ = std::thread([this]() {
        event_base_dispatch(base_);
    });

    LOG_INFO("RpcClient: connected to %s:%d", ip.c_str(), port);
    return true;
}

void RpcClient::Disconnect()
{
    running_.store(false);

    if (bev_) {
        bufferevent_free(bev_);
        bev_ = nullptr;
    }

    if (send_ev_) {
        event_free(send_ev_);
        send_ev_ = nullptr;
    }

    if (base_) {
        event_base_loopbreak(base_);
    }

    if (event_thread_.joinable()) {
        event_thread_.join();
    }

    connected_.store(false);

    // Reject all pending requests
    std::lock_guard<std::mutex> lock(pending_mutex_);
    for (auto& [id, req] : pending_) {
        RpcResponse err_resp;
        err_resp.request_id = id;
        err_resp.status_code = -1;
        err_resp.error_message = "connection closed";
        try {
            req.promise.set_value(err_resp);
        } catch (...) {}
    }
    pending_.clear();
}

void RpcClient::EnqueueWrite(const std::string& data)
{
    {
        std::lock_guard<std::mutex> lock(send_mutex_);
        send_queue_.push(data);
    }
    event_active(send_ev_, EV_WRITE, 0);
}

RpcResponse RpcClient::Call(const RpcRequest& request, int timeout_ms)
{
    auto t_start = std::chrono::steady_clock::now();

    auto future = CallAsync(request);
    auto status = future.wait_for(std::chrono::milliseconds(timeout_ms));

    RpcResponse result;
    if (status == std::future_status::timeout) {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_.erase(request.request_id);

        result.request_id = request.request_id;
        result.status_code = -1;
        result.error_message = "request timeout";
    } else {
        result = future.get();
    }

    auto t_end = std::chrono::steady_clock::now();
    int64_t latency_us = std::chrono::duration_cast<std::chrono::microseconds>(
        t_end - t_start).count();
    MetricsCollector::Instance().RecordClientLatencyUs(latency_us);
    MetricsCollector::Instance().RecordRequest();
    if (result.status_code == 0)
        MetricsCollector::Instance().RecordSuccess();
    else
        MetricsCollector::Instance().RecordError();

    return result;
}

RpcResponse RpcClient::Call(const std::string& service, const std::string& method,
                            const std::string& payload, int timeout_ms)
{
    RpcRequest req;
    req.request_id = next_request_id_.fetch_add(1);
    req.service = service;
    req.method = method;
    req.payload = payload;
    req.timeout_ms = timeout_ms;
    req.serialization = SerializationType::Protobuf;
    req.compression = CompressionType::None;
    req.encryption = EncryptionType::None;
    return Call(req, timeout_ms);
}

std::future<RpcResponse> RpcClient::CallAsync(const RpcRequest& request)
{
    std::string frame;
    std::string error;
    if (!EncodeRequest(request, codec_options_, frame, &error)) {
        RpcResponse err_resp;
        err_resp.request_id = request.request_id;
        err_resp.status_code = -1;
        err_resp.error_message = "encode failed: " + error;

        std::promise<RpcResponse> fallback;
        fallback.set_value(err_resp);
        return fallback.get_future();
    }

    // Extract future BEFORE adding to pending_, so that even if the
    // response arrives before we return, the future is still valid.
    PendingRequest pending;
    auto future = pending.promise.get_future();

    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_[request.request_id] = std::move(pending);
    }

    EnqueueWrite(frame);

    return future;
}

void RpcClient::read_callback(struct bufferevent* bev, void* ctx)
{
    RpcClient* client = static_cast<RpcClient*>(ctx);
    struct evbuffer* input = bufferevent_get_input(bev);

    while (evbuffer_get_length(input) >= FrameHeader::header_size) {
        char header_buf[FrameHeader::header_size];
        evbuffer_copyout(input, header_buf, FrameHeader::header_size);

        FrameHeader header;
        if (!DecodeHeader(std::string(header_buf, FrameHeader::header_size), header)) {
            bufferevent_free(bev);
            return;
        }

        size_t total_needed = FrameHeader::header_size + header.body_length;
        if (evbuffer_get_length(input) < total_needed) {
            return; // wait for more data
        }

        evbuffer_drain(input, FrameHeader::header_size);
        std::string body;
        body.resize(header.body_length);
        evbuffer_remove(input, &body[0], header.body_length);

        std::string error;
        DecodedFrame decoded;
        if (!VerifyAndDecodeFrame(header, body, decoded, CodecOptions(), &error)) {
            LOG_ERROR("RpcClient: decode error: %s", error.c_str());
            continue;
        }

        if (decoded.type == MessageType::Response || decoded.type == MessageType::Heartbeat) {
            RpcResponse response = (decoded.type == MessageType::Heartbeat)
                ? RpcResponse{decoded.response.request_id, 0, "", "",
                              SerializationType::Protobuf, CompressionType::None, EncryptionType::None}
                : decoded.response;

            std::lock_guard<std::mutex> lock(client->pending_mutex_);
            auto it = client->pending_.find(response.request_id);
            if (it != client->pending_.end()) {
                it->second.promise.set_value(response);
                client->pending_.erase(it);
            }
        }
    }
}

void RpcClient::event_callback(struct bufferevent* /*bev*/, short events, void* ctx)
{
    RpcClient* client = static_cast<RpcClient*>(ctx);

    if (events & BEV_EVENT_CONNECTED) {
        client->connected_.store(true);
        return;
    }

    if (events & (BEV_EVENT_ERROR | BEV_EVENT_EOF)) {
        client->connected_.store(false);
    }
}

void RpcClient::send_event_callback(evutil_socket_t /*fd*/, short /*events*/, void* arg)
{
    RpcClient* client = static_cast<RpcClient*>(arg);
    // Drain the entire send queue: EnqueueWrite may be called many times
    // before this callback fires, and event_active() calls can coalesce.
    while (true) {
        std::string data;
        {
            std::lock_guard<std::mutex> lock(client->send_mutex_);
            if (!client->send_queue_.empty()) {
                data = std::move(client->send_queue_.front());
                client->send_queue_.pop();
            }
        }
        if (data.empty() || !client->bev_) break;
        bufferevent_write(client->bev_, data.data(), data.size());
    }
}

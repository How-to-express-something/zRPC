# RPC Framework 架构分析与问题总结

## 目录

1. [Bug 总结与根因分析](#1-bug-总结与根因分析)
2. [重新设计的思路](#2-重新设计的思路)
3. [多线程环境下指针所有权与内存管理](#3-多线程环境下指针所有权与内存管理)
4. [框架整体架构](#4-框架整体架构)

---

## 1. Bug 总结与根因分析

### Bug 1：RpcSession 内存泄漏

**现象**：客户端断开连接后，`RpcSession` 对象及其持有的 `send_ev_` event 从未释放。

**根因**：[include/rpcserver.h:65](include/rpcserver.h#L65) 中 `CreateBufferevent` 用 `new RpcSession(bev)` 分配 session，但在 [server_event_callback:207](include/rpcserver.h#L207) 中仅调用了 `bufferevent_free(bev)`，`RpcSession` 和其内部的 `send_ev_`（通过 `event_new` 分配）均未释放。

**解决**：
- 在 `server_event_callback` 中先 `event_free(session->send_ev_)`，再 `delete session`，最后 `bufferevent_free(bev)`
- 释放后指针置 `nullptr` 防止 dangling

```cpp
static void server_event_callback(struct bufferevent *bev, short events, void *ctx) {
    RpcSession* session = static_cast<RpcSession*>(ctx);
    if (events & (BEV_EVENT_ERROR | BEV_EVENT_EOF)) {
        if (session) {
            event_free(session->send_ev_);
            delete session;
        }
        bufferevent_free(bev);
    }
}
```

**教训**：任何 `new` 出来的资源必须与 `delete` 成对出现。在涉及 libevent 回调时，尤其注意 `bufferevent_free` 只释放了 socket 层，不释放用户通过 `ctx` 传入的上下文。

---

### Bug 2：error / response 原始指针泄漏

**现象**：`server_read_callback` 中多个提前 return 路径只 `delete error` 但未处理 `response`；lambda 中如果 handler 抛出异常，`response` 和 `error` 泄漏。

**根因**：[include/rpcserver.h:238](include/rpcserver.h#L238) 使用 `std::string* error = new std::string` 和 `RpcResponse* response = new RpcResponse`，所有权靠手动 `delete` 维护。代码有多条提前 return 路径（心跳、handler 未找到、编码失败），各路径的 `delete` 不一致。lambda 中如果 handler 抛出异常，`delete` 语句会被跳过。

**解决**：改用 `std::shared_ptr`，靠 RAII 自动管理生命周期。

```cpp
// 修改前
std::string* error = new std::string;
RpcResponse* response = new RpcResponse;
thread_pool->submit([response, error]() {
    handler(...);
    delete response;
    delete error;
});

// 修改后
auto error = std::make_shared<std::string>();
auto response = std::make_shared<RpcResponse>();
thread_pool->submit([response, error, ...]() {
    handler(...);
    // shared_ptr 自动释放
});
```

**教训**：在多线程代码中，避免原始指针的跨线程手动管理。优先使用 `shared_ptr`（多个 lambda 共享）或 `unique_ptr`（单一所有权）。

---

### Bug 3：Static 成员导致多 Server 实例互串

**现象**：启动两个 RPCServer 实例时，所有 RPC 请求都被第一个 Server 处理，负载均衡失效。

**根因**：[include/rpcserver.h:105-106](include/rpcserver.h#L105-L106) 中 `WorkerReactor::thread_pool` 和 `handlers_` 是 `static` 成员变量。两个 RPCServer 创建各自的 `MainReactor` 时，都会调用 `WorkerReactor::set_handlers()`，第二个 Server 的调用覆盖了第一个。结果两个 Server 的所有 Worker 线程都使用同一个 handler map。

**解决**：去掉 `static` 声明，改为实例成员，在 `WorkerReactor` 构造函数中通过参数注入。同时将 handlers 和 thread_pool 指针存入 `RpcSession`，以便静态回调函数能访问。

```cpp
// 修改前
class WorkerReactor {
    static ThreadPool* thread_pool;
    static SafeHandlerMap* handlers_;
};

// 修改后
class WorkerReactor {
    ThreadPool* thread_pool_;
    SafeHandlerMap* handlers_;
};

class RpcSession {
    ThreadPool* thread_pool_;
    SafeHandlerMap* handlers_;
};
```

**教训**：`static` 成员变量在进程内全局唯一。当需要多个服务实例独立运行时，必须使用实例成员。框架类中避免使用 `static` 存储服务实例相关状态。

---

### Bug 4：帧头解码时 char[] → std::string 截断

**现象**：客户端连接后发送请求，服务器接收 50 字节（含 28 字节帧头），但 `DecodeHeader` 返回 false，连接被关闭。

**根因**：[include/rpcserver.h:216](include/rpcserver.h#L216) 中使用 `char header_buf[28]` 接收二进制帧头，然后直接传给 `DecodeHeader(header_buf, header)`。`char[]` 隐式转换为 `std::string` 时，`std::string` 构造函数遇到 `\0` 字节就截断。帧头中 `flags=0`、`encryption_type=0` 等字段是零值，导致字符串被截断为 3 字节（`magic_number=0x5250` + `version=1` 之后就是 `flags=0`）。

```cpp
// 错误用法
char header_buf[28];
evbuffer_copyout(input, header_buf, 28);
DecodeHeader(header_buf, header);  // header_buf → std::string 在 \0 处截断

// 正确用法
DecodeHeader(std::string(header_buf, 28), header);  // 显式指定长度
```

**解决**：在构造 `std::string` 时显式指定长度 `std::string(header_buf, FrameHeader::header_size)`，确保二进制数据完整传递。

**教训**：`const char*` 隐式转换为 `std::string` 时以 `\0` 为终止符。处理二进制数据时必须显式指定长度。所有涉及帧头解码的回调（包括 RpcClient 的 read_callback）都需要此修复。

**有趣的是**：原始 `main.cpp` 的测试中用的是 `frame.substr(0, FrameHeader::header_size)`，返回的 `std::string` 本身包含所有字节（substr 不截断），所以测试通过了。但实际网络回调中用的是 `char[]`，因此测试覆盖率不足，隐藏了此 Bug。

---

### Bug 5：register_typedhandler 空壳

**现象**：调用 `register_typedhandler` 注册类型化 handler 后，RPC 请求的 payload 不会被反序列化成 typed message，handler 直接返回 true。

**根因**：原始实现中：
- 参数 `handler` 签名错误：`std::function<bool(const RequestType&, const ResponseType&, std::string error)>` — `error` 按值传递，`ResponseType` 是 `const` 无法修改
- wrapper lambda 捕获 handler 但直接 `return true`，不调用 handler

**解决**：重写为正确的序列化/反序列化流程：

```cpp
Handler wrapper = [handler](const RpcRequest& rpc_req, RpcResponse& rpc_resp,
                             std::string* error) -> bool {
    RequestType req;
    ResponseType resp;

    if (!DeserializeMessage(rpc_req.payload, rpc_req.serialization, req, error))
        return false;

    if (!handler(req, resp, error))
        return false;

    if (!SerializeMessage(resp, rpc_req.serialization, rpc_resp.payload, error))
        return false;

    rpc_resp.request_id = rpc_req.request_id;  // 注意：此句最初遗漏，导致 Bug 6
    rpc_resp.status_code = 0;
    rpc_resp.serialization = rpc_req.serialization;
    return true;
};
```

---

### Bug 6：Typed Handler 未传递 request_id

**现象**：类型化 handler 处理后，服务器发送响应，但客户端收到 `request_id=0` 的响应，无法匹配到挂起的请求，最终超时。

**根因**：`register_typedhandler` 的 wrapper 中处理完 handler 后，填充 `rpc_resp` 时忘记了设置 `rpc_resp.request_id = rpc_req.request_id`。`EncodeResponse` 从 `response.request_id` 读取并写入帧头，因此响应帧头的 `request_id=0`。客户端用非零的 request_id 查找挂起请求，找不到匹配项。

**解决**：在 wrapper 中增加 `rpc_resp.request_id = rpc_req.request_id;`。

---

### Bug 7：EncodeResponse / DecodeResponse 字段顺序不匹配

**现象**：当 `status_code` 非零或 `error_message` 非空时，解码结果与编码数据不一致。

**根因**：[coder.cpp:201-204](coder.cpp#L201-L204) 中 `EncodeResponse` 的 body 布局为：

```
[uint16 error_message_len][int32 status_code][error_message][payload]
```

但 [coder.cpp:291-292](coder.cpp#L291-L292) 中 `DecodeResponse` 的读取顺序为：

```
[int32 status_code][uint16 error_message_len]  ← 顺序不一致！
```

当 `error_message` 为空且 `status_code=0` 时，body 前 6 字节全为零，两种读取方式巧合结果相同。但一旦 `status_code` 非零或 `error_message` 非空，解码结果完全错误。

**解决**：统一编码和解码的字段顺序。将编码顺序改为与解码一致（`status_code` 在前）：

```cpp
// EncodeResponse
WriteInt32(body, response.status_code);        // bytes 0-3
WriteUint16(body, response.error_message.size()); // bytes 4-5
body.append(response.error_message);
body.append(response.payload);
```

已于 [coder.cpp:201-202](coder.cpp#L201-L202) 修复。

---

## 2. 重新设计的思路

### 2.1 序列化层集成

原始代码中 `serialzation.cpp` 实现了 `ProtobufSerializer` 和 `JsonSerializer`，但从未在编解码路径中被调用。`RpcRequest.payload` 和 `RpcResponse.payload` 被视为"原始字节流"，直接拼接和读取。

**设计变更**：接入序列化层，使 RPC 框架支持类型化的 protobuf 调用：

```
客户端                              服务器
  │                                   │
  │  client.Call<AddReq, AddResp>()   │
  │  ├── SerializeMessage(req)        │
  │  ├── EncodeRequest(frame)         │
  │  │     payload = req_bytes        │
  │  └── 发送 network                 │
  │                                   │
  │                          server_read_callback
  │                            ├── DecodeRequest
  │                            │     payload = req_bytes
  │                            ├── wrapper → DeserializeMessage
  │                            ├── handler(req, resp)
  │                            ├── wrapper → SerializeMessage
  │                            ├── EncodeResponse
  │                            └── session->SendResponse
  │                                   │
  │  read_callback                    │
  │  ├── DecodeResponse               │
  │  ├── match request_id             │
  │  └── DeserializeMessage(resp)     │
  │      → typed response             │
```

### 2.2 内存管理改进

原始代码在多个地方使用 `new`/`delete` 原始指针，跨线程传递时生命周期难以保证。使用 `shared_ptr` 让 lambda 捕获时带引用计数，自动管理。

### 2.3 Static 成员 → 依赖注入

原来的 `WorkerReactor` 通过 `static` 方法设置全局的 `thread_pool` 和 `handlers_` 指针。改为在构造函数中通过参数传入，每个 `WorkerReactor` 持有自己的引用。同时将这些指针存入 `RpcSession`，因为静态回调函数需要访问它们。

---

## 3. 多线程环境下指针所有权与内存管理

### 3.1 线程模型分析

本框架有三种线程角色：

```
Main Reactor Thread (1 个)
  └── 接受连接 → 分发给 Worker（通过 pipe 通知）

Worker Reactor Threads (N 个)
  └── 运行 event_base_dispatch
  └── 处理 I/O 事件（read / write）
  └── 接收到完整帧后 → 提交到 ThreadPool

ThreadPool Threads (M 个)
  └── 执行业务 handler
  └── 处理完成后调用 session->SendResponse()
```

### 3.2 RpcSession 生命周期分析

`RpcSession` 的创建和销毁跨越三个线程：

```
创建: Worker 线程 (CreateBufferevent)
  → new RpcSession(bev, pool, handlers)
  → RpcSession.bev_ 在 Worker 线程的 event_base 上

使用:
  → Worker 线程: server_read_callback (读请求)
  → ThreadPool 线程: handler 执行完毕, 调用 session->SendResponse()
     → event_active(send_ev_, ...) 通知 Worker 线程
  → Worker 线程: SendEventCallback 发送响应

销毁:
  → Worker 线程: server_event_callback
     → event_free(send_ev_)
     → delete session
     → bufferevent_free(bev)
```

### 3.3 所有权管理方案

当前方案使用**原始指针 + 手动管理**，存在一定风险：

```cpp
// CreateBufferevent 中创建
RpcSession* session = new RpcSession(bev, thread_pool_, handlers_);

// 传递给 libevent 回调作为 ctx
bufferevent_setcb(bev, server_read_callback, nullptr,
                  server_event_callback, session);

// 在 server_event_callback 中销毁
delete session;
```

**此方案的问题**：
- `thread_pool_->submit()` 的 lambda 捕获了 `session` 原始指针。如果 session 在 lambda 执行前被销毁（连接断开），lambda 中的 `session->SendResponse()` 是野指针访问

**更优的方案**：使用 `std::shared_ptr<RpcSession>`

```cpp
// 1. 创建时使用 shared_ptr
auto session = std::make_shared<RpcSession>(bev, pool, handlers);

// 2. 保存一份到 WorkerReactor 的 connection 映射
connection_map_[fd] = session;  // WorkerReactor 持有引用

// 3. 传给 libevent 时使用原始指针（libevent 回调只接受原始指针）
//    但保证 session 不会提前释放——WorkerReactor 的 map 持有引用
bufferevent_setcb(bev, read_cb, nullptr, event_cb, session.get());

// 4. lambda 中捕获 shared_ptr
thread_pool_->submit([session, ...]() {
    session->SendResponse(frame);
});

// 5. 连接断开时从 map 中移除
connection_map_.erase(fd);  // map 释放引用，若 lambda 还在执行则不会销毁
```

**关键设计原则**：
1. **WorkerReactor 拥有 session 的生命周期主引用**（存在 map 中）
2. **ThreadPool lambda 持有 session 的临时引用**（shared_ptr 捕获）
3. **Libevent 回调使用原始指针**（由主引用保证不被提前释放）
4. 销毁时：Worker 线程从 map 移除，如果没有任何 lambda 持有 session，则自动析构

### 3.4 实际采用的方案

当前实现考虑工程简洁性，采用 **Worker 线程独有 + 同步销毁**的方案：

- `RpcSession` 由 `WorkerReactor` 的 `CreateBufferevent` 创建（在 Worker 线程的上下文中）
- 只在 `server_event_callback` 中销毁（也在 Worker 线程中）
- ThreadPool 中的 lambda 虽然跨线程调用 `SendResponse`，但 `SendResponse` 只做两件事：
  1. 数据 push 到队列（mutex 保护）  
  2. `event_active(send_ev_, ...)` 通知 Worker 线程（线程安全）

实际发送由 Worker 线程的 `SendEventCallback` 完成。这意味着 `session` 的成员 `bev_` 和 `send_ev_` 只在 Worker 线程中访问，无需跨线程保护。

**风险**：如果 ThreadPool lambda 正在执行时 `server_event_callback` 被调用，lambda 中的 `session->SendResponse()` 访问已释放的 session。此概率较低（连接断开时 ThreadPool 中的任务应该已经完成或立即完成），但理论上存在。

**改进方向**：若需完全消除此风险，应采用上节的 `shared_ptr + map` 方案。

### 3.5 通用多线程指针管理原则

| 场景 | 推荐方案 | 说明 |
|------|----------|------|
| 单一线程创建和销毁 | `unique_ptr` | 所有权明确，零开销 |
| 多线程共享，生命周期不确定 | `shared_ptr` | 引用计数自动管理 |
| 跨线程回调，但回调在同一个 EventLoop | 原始指针 + EventLoop 保证 | EventLoop 保证回调执行时对象存活 |
| 跨线程 async 任务 | `shared_ptr` | lambda 捕获增加引用计数 |
| 线程池任务引用对象 | `shared_ptr` | 任务执行期间对象必须存活 |
| libevent/其他 C 风格回调 | 原始指针 + 生命期担保 | 外部容器持有 shared_ptr 防止提前释放 |

---

## 4. 框架整体架构

```
┌─────────────────────────────────────────────────────────┐
│                      RPCServer                           │
│  ┌──────────────────────────────────────────────────┐   │
│  │  MainReactor (1 thread)                          │   │
│  │  ├── listener_callback: accept TCP               │   │
│  │  └── round-robin → WorkerReactor                 │   │
│  ├──────────────────────────────────────────────────┤   │
│  │  WorkerReactor[0..N-1] (N threads)               │   │
│  │  ├── 每个 Worker 一个 event_base                  │   │
│  │  ├── RpcSession: 1 session per connection         │   │
│  │  ├── server_read_callback → 解析帧头/帧体         │   │
│  │  └── 完整帧 → thread_pool.submit(handler)        │   │
│  ├──────────────────────────────────────────────────┤   │
│  │  ThreadPool (M threads)                           │   │
│  │  └── 执行 Handler → EncodeResponse               │   │
│  │       → session->SendResponse()                  │   │
│  └──────────────────────────────────────────────────┘   │
│                                                          │
│  SafeHandlerMap: "service#method" → Handler function     │
│  register_handler / register_typedhandler                │
└──────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│                      RpcClient                           │
│  ├── event_base + event loop thread                     │
│  ├── Connect() → bufferevent connect                    │
│  ├── Call() → EnqueueWrite → event_active → 等待 future │
│  └── read_callback → match request_id → promise.set_value│
└──────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│                  ServiceRegistry                         │
│  ├── service_name → [endpoint1, endpoint2, ...]          │
│  └── Thread-safe (mutex)                                │
├──────────────────────────────────────────────────────────┤
│                  LoadBalancer                            │
│  ├── RoundRobin / Random                                │
│  └── GetEndpoint(service_name) → endpoint                │
└──────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│                  Codec (coder.h/cpp)                     │
│  ├── EncodeRequest / EncodeResponse                     │
│  ├── DecodeRequest / DecodeResponse                     │
│  ├── VerifyAndDecodeFrame (CRC32 校验)                  │
│  └── ProcessEncrypt / MaybeCompress                     │
├─────────────────────────────────────────────────────────┤
│                  Serialization (serialzation.h/cpp)      │
│  ├── ProtobufSerializer / JsonSerializer                │
│  └── SerializeMessage / DeserializeMessage              │
└──────────────────────────────────────────────────────────┘
```

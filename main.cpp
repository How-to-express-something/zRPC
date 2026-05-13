/**
 * libevent 完整教学示例
 * 编译: g++ -o libevent_tutorial main.cpp -levent -levent_pthreads -lpthread
 * 运行: ./libevent_tutorial [选项]
 * 
 * 功能包括:
 * 1. 基础定时器
 * 2. 信号处理
 * 3. TCP服务器
 * 4. TCP客户端
 * 5. HTTP服务器
 * 6. 多线程示例
 */

#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/thread.h>
#include <event2/http.h>
#include <event2/http_struct.h>
#include <iostream>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include <signal.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define PORT 8888
#define HTTP_PORT 8080

using namespace std;

// ==================== 1. 基础定时器示例 ====================
void timer_callback(evutil_socket_t fd, short events, void *arg) {
    static int count = 0;
    struct event_base *base = (struct event_base*)arg;
    
    cout << "[定时器] 触发次数: " << ++count << endl;
    
    if (count >= 5) {
        cout << "[定时器] 到达5次,退出事件循环" << endl;
        event_base_loopexit(base, nullptr);
    }
}

void test_timer() {
    cout << "\n========== 1. 基础定时器测试 ==========" << endl;
    
    struct event_base *base = event_base_new();
    if (!base) {
        cerr << "创建事件基失败!" << endl;
        return;
    }
    
    cout << "使用后端: " << event_base_get_method(base) << endl;
    
    // 创建定时器，1秒后触发
    struct timeval tv = {1, 0};  // 1秒
    struct event *timer_ev = evtimer_new(base, timer_callback, base);
    evtimer_add(timer_ev, &tv);
    
    cout << "定时器已启动，将在1秒后触发，共5次..." << endl;
    
    // 进入事件循环
    event_base_dispatch(base);
    
    // 清理
    event_free(timer_ev);
    event_base_free(base);
}

// ==================== 2. 信号处理示例 ====================
void signal_callback(evutil_socket_t sig, short events, void *arg) {
    struct event_base *base = (struct event_base*)arg;
    cout << "\n[信号] 收到信号: " << sig << " (SIGINT: Ctrl+C)" << endl;
    cout << "[信号] 正在退出..." << endl;
    event_base_loopexit(base, nullptr);
}

void test_signal() {
    cout << "\n========== 2. 信号处理测试 ==========" << endl;
    
    struct event_base *base = event_base_new();
    
    // 创建信号事件，处理SIGINT (Ctrl+C)
    struct event *signal_ev = evsignal_new(base, SIGINT, signal_callback, base);
    if (!signal_ev) {
        cerr << "创建信号事件失败!" << endl;
        event_base_free(base);
        return;
    }
    event_add(signal_ev, nullptr);
    
    cout << "信号处理器已注册，按 Ctrl+C 测试信号处理" << endl;
    cout << "5秒后自动退出..." << endl;
    
    // 5秒后自动退出
    struct timeval tv = {5, 0};
    struct event *timer_ev = evtimer_new(base, 
        [](evutil_socket_t, short, void *arg) {
            cout << "\n[定时器] 5秒已到，自动退出" << endl;
            event_base_loopexit((struct event_base*)arg, nullptr);
        }, base);
    evtimer_add(timer_ev, &tv);
    
    event_base_dispatch(base);
    
    event_free(signal_ev);
    event_free(timer_ev);
    event_base_free(base);
}

// ==================== 3. TCP服务器示例 ====================
// 读取回调
void server_read_callback(struct bufferevent *bev, void *ctx) {
    struct evbuffer *input = bufferevent_get_input(bev);
    char data[1024];
    
    while (evbuffer_get_length(input) > 0) {
        int len = evbuffer_remove(input, data, sizeof(data) - 1);
        if (len > 0) {
            data[len] = '\0';
            cout << "[TCP服务器] 收到消息: " << data;
            
            // 回显消息
            string response = "服务器回应: " + string(data);
            bufferevent_write(bev, response.c_str(), response.length());
        }
    }
}

// 事件回调
void server_event_callback(struct bufferevent *bev, short events, void *ctx) {
    if (events & BEV_EVENT_ERROR) {
        cerr << "[TCP服务器] 连接错误!" << endl;
    }
    if (events & BEV_EVENT_EOF) {
        cout << "[TCP服务器] 客户端断开连接" << endl;
    }
    if (events & BEV_EVENT_CONNECTED) {
        cout << "[TCP服务器] 新客户端连接" << endl;
    }
    
    if (events & (BEV_EVENT_ERROR | BEV_EVENT_EOF)) {
        bufferevent_free(bev);
    }
}

// 监听回调
void listener_callback(struct evconnlistener *listener, evutil_socket_t fd,
                       struct sockaddr *sa, int socklen, void *user_data) {
    cout << "[TCP服务器] 新连接接入!" << endl;
    
    struct event_base *base = (struct event_base*)user_data;
    struct bufferevent *bev = bufferevent_socket_new(base, fd, 
                                                      BEV_OPT_CLOSE_ON_FREE);
    
    bufferevent_setcb(bev, server_read_callback, NULL, server_event_callback, NULL);
    bufferevent_enable(bev, EV_READ | EV_WRITE);
}

void test_tcp_server() {
    cout << "\n========== 3. TCP服务器测试 ==========" << endl;
    
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(PORT);
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    
    struct event_base *base = event_base_new();
    
    struct evconnlistener *listener = evconnlistener_new_bind(
        base, listener_callback, base,
        LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE,
        10, (struct sockaddr*)&sin, sizeof(sin));
    
    if (!listener) {
        cerr << "[TCP服务器] 创建监听器失败!" << endl;
        event_base_free(base);
        return;
    }
    
    cout << "[TCP服务器] 监听端口: " << PORT << endl;
    cout << "[TCP服务器] 测试命令: telnet 127.0.0.1 " << PORT << endl;
    cout << "[TCP服务器] 10秒后自动退出..." << endl;
    
    // 10秒后自动退出
    struct timeval tv = {10, 0};
    struct event *timer_ev = evtimer_new(base, 
        [](evutil_socket_t, short, void *arg) {
            cout << "\n[TCP服务器] 10秒已到，退出" << endl;
            event_base_loopexit((struct event_base*)arg, nullptr);
        }, base);
    evtimer_add(timer_ev, &tv);
    
    event_base_dispatch(base);
    
    evconnlistener_free(listener);
    event_free(timer_ev);
    event_base_free(base);
}

// ==================== 4. TCP客户端示例 ====================
// 客户端读取回调
void client_read_callback(struct bufferevent *bev, void *ctx) {
    struct evbuffer *input = bufferevent_get_input(bev);
    char data[1024];
    int len = evbuffer_remove(input, data, sizeof(data) - 1);
    if (len > 0) {
        data[len] = '\0';
        cout << "[TCP客户端] 收到服务器响应: " << data;
    }
}

// 客户端事件回调
void client_event_callback(struct bufferevent *bev, short events, void *ctx) {
    struct event_base *base = (struct event_base*)ctx;
    
    if (events & BEV_EVENT_CONNECTED) {
        cout << "[TCP客户端] 连接成功!" << endl;
        
        // 发送消息
        string message = "Hello from client!\n";
        bufferevent_write(bev, message.c_str(), message.length());
        cout << "[TCP客户端] 发送: " << message;
    }
    
    if (events & BEV_EVENT_ERROR) {
        cerr << "[TCP客户端] 连接错误!" << endl;
    }
    if (events & BEV_EVENT_EOF) {
        cout << "[TCP客户端] 连接关闭" << endl;
    }
    
    if (events & (BEV_EVENT_ERROR | BEV_EVENT_EOF | BEV_EVENT_CONNECTED)) {
        // 2秒后退出
        struct timeval tv = {2, 0};
        event_base_loopexit(base, &tv);
    }
}

void test_tcp_client() {
    cout << "\n========== 4. TCP客户端测试 ==========" << endl;
    
    struct event_base *base = event_base_new();
    
    struct bufferevent *bev = bufferevent_socket_new(base, -1, 
                                                      BEV_OPT_CLOSE_ON_FREE);
    
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &sin.sin_addr);
    
    cout << "[TCP客户端] 连接到 127.0.0.1:" << PORT << endl;
    cout << "[TCP客户端] 请先启动TCP服务器!" << endl;
    
    bufferevent_setcb(bev, client_read_callback, NULL, client_event_callback, base);
    bufferevent_enable(bev, EV_READ);
    
    bufferevent_socket_connect(bev, (struct sockaddr*)&sin, sizeof(sin));
    
    event_base_dispatch(base);
    
    bufferevent_free(bev);
    event_base_free(base);
}

// ==================== 5. HTTP服务器示例 ====================
void http_request_callback(struct evhttp_request *req, void *arg) {
    struct evbuffer *buf = evbuffer_new();
    if (!buf) return;
    
    // 获取请求URI
    const char *uri = evhttp_request_get_uri(req);
    cout << "[HTTP服务器] 收到请求: " << uri << endl;
    
    // 构建响应HTML
    string html = "<!DOCTYPE html>\n"
                  "<html>\n"
                  "<head><title>libevent HTTP Server</title></head>\n"
                  "<body>\n"
                  "<h1>Hello from libevent HTTP Server!</h1>\n"
                  "<p>Request URI: " + string(uri) + "</p>\n"
                  "<p>Server time: " + to_string(time(nullptr)) + "</p>\n"
                  "<hr>\n"
                  "<h2>可用测试路径:</h2>\n"
                  "<ul>\n"
                  "  <li><a href='/'>/</a> - 首页</li>\n"
                  "  <li><a href='/test'>/test</a> - 测试页面</li>\n"
                  "  <li><a href='/api/data'>/api/data</a> - API示例</li>\n"
                  "</ul>\n"
                  "</body>\n"
                  "</html>\n";
    
    evbuffer_add(buf, html.c_str(), html.length());
    
    // 设置响应头
    evhttp_add_header(evhttp_request_get_output_headers(req), 
                      "Content-Type", "text/html");
    
    // 发送响应
    evhttp_send_reply(req, HTTP_OK, "OK", buf);
    evbuffer_free(buf);
}

void test_http_server() {
    cout << "\n========== 5. HTTP服务器测试 ==========" << endl;
    
    struct event_base *base = event_base_new();
    struct evhttp *http = evhttp_new(base);
    
    if (!http) {
        cerr << "[HTTP服务器] 创建失败!" << endl;
        event_base_free(base);
        return;
    }
    
    // 绑定端口
    if (evhttp_bind_socket(http, "0.0.0.0", HTTP_PORT) != 0) {
        cerr << "[HTTP服务器] 绑定端口失败!" << endl;
        evhttp_free(http);
        event_base_free(base);
        return;
    }
    
    // 设置请求回调
    evhttp_set_gencb(http, http_request_callback, NULL);
    
    cout << "[HTTP服务器] 启动成功!" << endl;
    cout << "[HTTP服务器] 监听端口: " << HTTP_PORT << endl;
    cout << "[HTTP服务器] 访问: http://127.0.0.1:" << HTTP_PORT << endl;
    cout << "[HTTP服务器] 按 Ctrl+C 停止" << endl;
    
    // 添加信号处理
    struct event *signal_ev = evsignal_new(base, SIGINT, 
        [](evutil_socket_t, short, void *arg) {
            cout << "\n[HTTP服务器] 收到停止信号" << endl;
            event_base_loopexit((struct event_base*)arg, nullptr);
        }, base);
    event_add(signal_ev, nullptr);
    
    event_base_dispatch(base);
    
    event_free(signal_ev);
    evhttp_free(http);
    event_base_free(base);
}

// ==================== 6. 多线程示例 ====================
volatile bool thread_running = true;
const int THREAD_NUM = 4;

void worker_thread(int id, struct event_base *base) {
    cout << "[线程" << id << "] 启动，使用后端: " 
         << event_base_get_method(base) << endl;
    
    // 创建定时器
    struct timeval tv = {2, 0};
    struct event *timer = evtimer_new(base, 
        [](evutil_socket_t, short, void *arg) {
            int *id_ptr = (int*)arg;
            cout << "[线程" << *id_ptr << "] 定时器触发" << endl;
        }, &id);
    evtimer_add(timer, &tv);
    
    // 循环处理事件
    while (thread_running) {
        event_base_loop(base, EVLOOP_NONBLOCK);
        this_thread::sleep_for(chrono::milliseconds(100));
    }
    
    event_free(timer);
}

void test_thread_pool() {
    cout << "\n========== 6. 多线程示例 ==========" << endl;
    
    // 初始化libevent线程支持
    evthread_use_pthreads();
    
    vector<event_base*> bases;
    vector<thread> threads;
    
    // 创建多个事件基，每个线程一个
    for (int i = 0; i < THREAD_NUM; i++) {
        struct event_base *base = event_base_new();
        bases.push_back(base);
        threads.emplace_back(worker_thread, i, base);
    }
    
    cout << "创建了 " << THREAD_NUM << " 个工作线程" << endl;
    cout << "5秒后停止..." << endl;
    
    this_thread::sleep_for(chrono::seconds(5));
    thread_running = false;
    
    // 等待线程结束
    for (auto& t : threads) {
        t.join();
    }
    
    // 清理
    for (auto base : bases) {
        event_base_free(base);
    }
    
    cout << "[多线程] 测试完成" << endl;
}

// ==================== 7. 性能测试 - 批量连接 ====================
void test_performance() {
    cout << "\n========== 7. 性能测试 - 批量事件 ==========" << endl;
    
    struct event_base *base = event_base_new();
    int event_count = 10000;
    
    cout << "创建 " << event_count << " 个定时器事件..." << endl;
    
    // 创建大量定时器
    vector<struct event*> timers;
    for (int i = 0; i < event_count; i++) {
        struct event *ev = evtimer_new(base, 
            [](evutil_socket_t, short, void *arg) {
                // 空回调，只测试性能
            }, nullptr);
        timers.push_back(ev);
    }
    
    // 设置1秒后触发
    struct timeval tv = {0, 100};  // 100ms
    for (auto ev : timers) {
        evtimer_add(ev, &tv);
    }
    
    cout << "事件已添加，运行事件循环1秒..." << endl;
    
    // 只运行一小段时间
    struct timeval run_tv = {1, 0};
    event_base_loopexit(base, &run_tv);
    
    auto start = chrono::high_resolution_clock::now();
    event_base_dispatch(base);
    auto end = chrono::high_resolution_clock::now();
    
    auto duration = chrono::duration_cast<chrono::milliseconds>(end - start);
    cout << "处理 " << event_count << " 个事件耗时: " 
         << duration.count() << " ms" << endl;
    
    // 清理
    for (auto ev : timers) {
        event_free(ev);
    }
    event_base_free(base);
}

// ==================== 主函数 ====================
void print_menu() {
    cout << "\n╔══════════════════════════════════════════════════════╗" << endl;
    cout << "║         libevent 完整教学示例                        ║" << endl;
    cout << "╠══════════════════════════════════════════════════════╣" << endl;
    cout << "║  1. 基础定时器                                       ║" << endl;
    cout << "║  2. 信号处理                                         ║" << endl;
    cout << "║  3. TCP服务器                                        ║" << endl;
    cout << "║  4. TCP客户端                                        ║" << endl;
    cout << "║  5. HTTP服务器                                       ║" << endl;
    cout << "║  6. 多线程示例                                       ║" << endl;
    cout << "║  7. 性能测试                                         ║" << endl;
    cout << "║  0. 运行所有测试                                     ║" << endl;
    cout << "╚══════════════════════════════════════════════════════╝" << endl;
    cout << "请选择: ";
}

int main(int argc, char *argv[]) {
    // 初始化socket库（Linux下可选，Windows必须）
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    
    cout << "\n=== libevent 教学示例 ===" << endl;
    cout << "libevent版本: " << event_get_version() << endl;
    
    int choice = 0;
    
    if (argc > 1) {
        choice = atoi(argv[1]);
    } else {
        print_menu();
        cin >> choice;
    }
    
    switch (choice) {
        case 1:
            test_timer();
            break;
        case 2:
            test_signal();
            break;
        case 3:
            test_tcp_server();
            break;
        case 4:
            test_tcp_client();
            break;
        case 5:
            test_http_server();
            break;
        case 6:
            test_thread_pool();
            break;
        case 7:
            test_performance();
            break;
        case 0:
            test_timer();
            test_signal();
            test_performance();
            test_thread_pool();
            // TCP和HTTP需要交互，单独测试
            cout << "\n注意: TCP和HTTP服务器需要单独测试" << endl;
            break;
        default:
            cout << "无效选择，运行基础测试..." << endl;
            test_timer();
            break;
    }
    
    cout << "\n=== 测试完成 ===" << endl;
    
#ifdef _WIN32
    WSACleanup();
#endif
    
    return 0;
}
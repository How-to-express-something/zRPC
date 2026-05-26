#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <sstream>
#include <event2/thread.h>

#include "coder.h"
#include "protocol.h"
#include "rpcserver.h"
#include "rpcclient.h"
#include "service_registry.h"
#include "person.pb.h"
#include "log.h"
#include "metrics.h"

// ==================== Demo: RPC Framework with Protobuf Serialization ====================

static std::atomic<int> server1_count{0};
static std::atomic<int> server2_count{0};

void run_server(int port, int server_id, std::atomic<bool>& ready)
{
    RPCServer server(port, "127.0.0.1", 1);

    // ---- Typed handler: Calculator.Add using Protobuf ----
    server.register_typedhandler<AddRequest, AddResponse>(
        "Calculator", "Add",
        [server_id, port](const AddRequest& req, AddResponse& resp, std::string* /*error*/) -> bool {
            if (server_id == 1) server1_count++;
            else server2_count++;

            resp.set_result(req.a() + req.b());

            LOG_DEBUG("[Server %d:%d] Calculator.Add(a=%d,b=%d) -> result=%d (total=%d)",
                      server_id, port, req.a(), req.b(), resp.result(),
                      server_id == 1 ? server1_count.load() : server2_count.load());
            return true;
        });

    // ---- Raw handler: Greeter.Hello using plain text ----
    server.register_handler("Greeter", "Hello",
        [server_id, port](const RpcRequest& req, RpcResponse& resp, std::string* /*error*/) -> bool {
            if (server_id == 1) server1_count++;
            else server2_count++;

            resp.request_id = req.request_id;
            resp.status_code = 0;
            resp.payload = "Hello, " + req.payload + "! (from server " + std::to_string(server_id) + ")";
            resp.serialization = SerializationType::Protobuf;
            resp.compression = CompressionType::None;
            resp.encryption = EncryptionType::None;

            LOG_DEBUG("[Server %d:%d] Greeter.Hello(\"%s\") -> \"%s\"",
                      server_id, port, req.payload.c_str(), resp.payload.c_str());
            return true;
        });

    LOG_INFO("[Server %d] Starting on port %d...", server_id, port);
    ready.store(true);
    server.start();
}

int main()
{
    evthread_use_pthreads();

    Logger::GetInstance().SetLevel(LogLevel::Debug);

    std::cout << "==========================================================\n";
    std::cout << "  RPC Framework Demo — Typed (Protobuf) + Raw Handlers\n";
    std::cout << "==========================================================\n\n";

    // ---- Phase 1: Start servers ----
    LOG_INFO("[Phase 1] Starting 2 RPC servers...");

    std::atomic<bool> server1_ready{false};
    std::atomic<bool> server2_ready{false};

    std::thread server1_thread(run_server, 9001, 1, std::ref(server1_ready));
    std::thread server2_thread(run_server, 9002, 2, std::ref(server2_ready));

    while (!server1_ready.load() || !server2_ready.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    LOG_INFO("[Phase 1] Both servers ready.");

    // ---- Phase 2: Service Registry ----
    LOG_INFO("[Phase 2] Service Registry...");
    ServiceRegistry registry;
    registry.Register("Calculator", {"127.0.0.1", 9001, 1});
    registry.Register("Calculator", {"127.0.0.1", 9002, 1});
    registry.Register("Greeter", {"127.0.0.1", 9001, 1});
    registry.Register("Greeter", {"127.0.0.1", 9002, 1});
    LOG_INFO("  Calculator: 2 backends, Greeter: 2 backends");

    // ---- Phase 3: Load Balancer ----
    LOG_INFO("[Phase 3] Load Balancer (RoundRobin)...");
    LoadBalancer lb(&registry, LoadBalanceStrategy::RoundRobin);

    // ---- Phase 4: Typed RPC calls (Protobuf serialization) ----
    std::cout << "\n==========================================================\n";
    std::cout << "[Phase 4] Typed RPC — Calculator.Add via Protobuf\n";
    std::cout << "==========================================================\n\n";

    RpcClient client;

    for (int i = 0; i < 6; i++) {
        auto endpoint = lb.GetEndpoint("Calculator");
        if (!endpoint) break;

        client.Connect(endpoint->ip, endpoint->port);

        AddRequest req;
        req.set_a(i * 10 + 1);
        req.set_b(i * 10 + 2);

        AddResponse resp;
        auto rpc_resp = client.Call("Calculator", "Add", req, resp,
                                     SerializationType::Protobuf, 5000);

        if (rpc_resp.status_code == 0) {
            LOG_INFO("Client <- %s : status=%d result=%d",
                     endpoint->addr().c_str(), rpc_resp.status_code, resp.result());
        } else {
            LOG_ERROR("Client <- %s : status=%d error=\"%s\"",
                      endpoint->addr().c_str(), rpc_resp.status_code, rpc_resp.error_message.c_str());
        }

        client.Disconnect();
    }

    // ---- Phase 5: Raw RPC calls (plain text payload) ----
    std::cout << "\n==========================================================\n";
    std::cout << "[Phase 5] Raw RPC — Greeter.Hello via plain text\n";
    std::cout << "==========================================================\n\n";

    const char* names[] = {"Alice", "Bob", "World", "RPC"};
    for (int i = 0; i < 4; i++) {
        auto endpoint = lb.GetEndpoint("Greeter");
        if (!endpoint) continue;

        client.Connect(endpoint->ip, endpoint->port);

        auto resp = client.Call("Greeter", "Hello", names[i], 3000);

        if (resp.status_code == 0) {
            LOG_INFO("Client <- %s : status=%d payload=\"%s\"",
                     endpoint->addr().c_str(), resp.status_code, resp.payload.c_str());
        } else {
            LOG_ERROR("Client <- %s : status=%d error=\"%s\"",
                      endpoint->addr().c_str(), resp.status_code, resp.error_message.c_str());
        }

        client.Disconnect();
    }

    // ---- Phase 6: Random strategy ----
    std::cout << "\n==========================================================\n";
    std::cout << "[Phase 6] Load Balancer — Random strategy\n";
    std::cout << "==========================================================\n\n";

    lb.SetStrategy(LoadBalanceStrategy::Random);
    for (int i = 0; i < 4; i++) {
        auto ep = lb.GetEndpoint("Calculator");
        if (ep) LOG_INFO("  Request #%d -> %s", i+1, ep->addr().c_str());
    }

    // ---- Phase 7: Offline round-trip test ----
    std::cout << "\n==========================================================\n";
    std::cout << "[Phase 7] Offline encode/decode round-trip (Protobuf)\n";
    std::cout << "==========================================================\n\n";

    {
        AddRequest req;
        req.set_a(100);
        req.set_b(200);

        std::string payload;
        std::string error;
        SerializeMessage(req, SerializationType::Protobuf, payload, &error);
        LOG_INFO("  Serialize AddRequest(100,200): %zu bytes", payload.size());

        RpcRequest rpc_req;
        rpc_req.request_id = 99;
        rpc_req.service = "Calculator";
        rpc_req.method = "Add";
        rpc_req.payload = payload;
        rpc_req.serialization = SerializationType::Protobuf;

        std::string frame;
        EncodeRequest(rpc_req, CodecOptions(), frame, &error);
        LOG_INFO("  Encode frame: %zu bytes", frame.size());

        FrameHeader header;
        DecodeHeader(std::string(frame.data(), FrameHeader::header_size), header);
        DecodedFrame decoded;
        VerifyAndDecodeFrame(header, frame.substr(FrameHeader::header_size),
                             decoded, CodecOptions(), &error);

        AddRequest req2;
        DeserializeMessage(decoded.request.payload, SerializationType::Protobuf, req2, &error);
        std::cout << "  Round-trip: a=" << req2.a() << " b=" << req2.b()
                  << "  " << (req2.a() == 100 && req2.b() == 200 ? "OK" : "FAIL") << "\n";
    }

    // ---- Summary ----
    std::cout << "\n==========================================================\n";
    std::cout << "  Summary\n";
    std::cout << "==========================================================\n";
    std::cout << "  Server 1 requests: " << server1_count.load() << "\n";
    std::cout << "  Server 2 requests: " << server2_count.load() << "\n";
    std::cout << "  Total: " << (server1_count.load() + server2_count.load()) << "\n";
    std::cout << "  Load balancing: "
              << (server1_count.load() > 0 && server2_count.load() > 0
                  ? "WORKING" : "check distribution") << "\n";
    std::cout << "  Typed handlers:  Calculator.Add (Protobuf AddRequest/AddResponse)\n";
    std::cout << "  Raw handlers:    Greeter.Hello (plain text payload)\n";

    // ---- Metrics Report ----
    std::cout << MetricsCollector::Instance().Report();

    std::cout << "[Cleanup] Done.\n";

    server1_thread.detach();
    server2_thread.detach();
    return 0;
}

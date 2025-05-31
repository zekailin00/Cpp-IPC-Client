
#include "RpcClient.h"
#include <iostream>

int main() {
    RpcClient rpcClient{};

    RpcResponse resp;
    std::string result;

    // resp = rpcClient.send
    // ("sub", 
    //     "{\"keys\": [\"a\", \"b\"], "
    //     "\"values\": [\"5\", \"1d2.2\"]}"
    // );
    // std::cout << "[C++] Response: " << resp.result_json << std::endl;

    // resp = rpcClient.send
    // ("mul", 
    //     "{\"keys\": [\"a\", \"b\"], "
    //     "\"values\": [\"5\", \"123.2\"]}"
    // );
    // std::cout << "[C++] Response: " << resp.result_json << std::endl;

    // resp = rpcClient.send
    // ("echo", 
    //     "{\"keys\": [\"text\", \"b\"], "
    //     "\"values\": [\"c\", \"10\"]}"
    // );
    // std::cout << "[C++] Response: " << resp.result_json << std::endl;

    result = rpcClient.Call("sub", {
        {"a", 2},
        {"b", 3}
    });
    std::cout << "[C++] Response: " << result << std::endl;

    result = rpcClient.Call("mul", {
        {"a", 2},
        {"b", 23.2}
    });
    std::cout << "[C++] Response: " << result << std::endl;

    result = rpcClient.Call("echo", {
        {"text", "echo back"}
    });
    std::cout << "[C++] Response: " << result << std::endl;

    result = rpcClient.Call("do_work",
    {
        {"input", "Hello from C++"},
        {"delay", 3000} // simulate work on Unity side
    },
    {
        {"onComplete", [](const nlohmann::json& result) {
            std::cout << "[Callback] Received result from Unity: " << result.dump() << std::endl;
        }}
    });
    std::cout <<  "[C++] Response: " << result << std::endl;

    return 0;

    /**
     * 1. RPC callback
     * 2. Debug Window API
     * 3. Basic Platform API support
     * 4. Master process + child Process Spawn
     * 5. Multi-client communication using Semaphore
     */
}

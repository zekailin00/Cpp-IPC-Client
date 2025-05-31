
#include "RpcClient.h"
#include <iostream>

int main() {
    RpcClient rpcClient{};
    rpcClient.ListenForCallbacks();

    nlohmann::json result;

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

    int handle = rpcClient.Call("timer",
    {
        {"text", "Hello from C++"},
        {"interval", 1000} // simulate work on Unity side
    },
    {
        {"callback", [](const nlohmann::json& result) {
            std::cout << "[Callback] Received result from Unity: " << result.dump() << std::endl;
        }}
    });
    std::cout <<  "[C++] Timer response: " << result << std::endl;

    Sleep(5000);

    printf("Handle ID: %d", handle);
    rpcClient.Call("dispose_timer", {{"timerHandle", handle}});

    return 0;

    /**
     * 1. RPC callback
     * 2. Debug Window API
     * 3. Basic Platform API support
     * 4. Master process + child Process Spawn / deallocation
     * 5. Multi-client communication using Semaphore
     */
}

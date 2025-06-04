
#include "RpcClient.h"
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

int main() {
    RpcClient rpcClient{};

    nlohmann::json result;

    result = rpcClient.Call("sub", {
        {"a", 2},
        {"b", 3}
    });
    std::cout << "[C++] sub Response: " << result << std::endl;

    result = rpcClient.Call("mul", {
        {"a", 2},
        {"b", 23.2}
    });
    std::cout << "[C++] mul Response: " << result << std::endl;

    result = rpcClient.Call("echo", {
        {"text", "echo back"}
    });
    std::cout << "[C++] echo Response: " << result << std::endl;

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
    std::cout <<  "[C++] do_work Response: " << result << std::endl;

    int handle = rpcClient.Call("timer",
    {
        {"text", rpcClient.GetClientId()},
        {"interval", 1000} // simulate work on Unity side
    },
    {
        {"callback", [&](const nlohmann::json& result) {
            rpcClient.Call("AddToCounter", {{"value", 1}});
            std::cout << "[Callback] Received result from Unity: " << result.dump() << std::endl;
        }}
    });
    std::cout <<  "[C++] Timer response: " << handle << std::endl;

#ifdef _WIN32
    Sleep(5000);
#else    
    sleep(5);
#endif

    printf("Handle ID: %d\n", handle);
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

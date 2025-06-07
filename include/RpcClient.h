#pragma once

#ifdef _WIN32
#define NOMINMAX
#include <WinSock2.h>
#pragma comment(lib, "Ws2_32.lib")  // Optional backup
#define SOCKET_TYPE SOCKET
#else
#define SOCKET_TYPE int
#endif

#include <string>
#include <functional>
#include <unordered_map>
#include <thread>
#include <mutex>

#include <nlohmann/json.hpp>


struct RpcRequest
{
    struct {
        int clientId;
        char functionName[64];
        int bufferSize;
    } header;

    std::string jsonArgs;
};

struct ResponseHeader
{
    enum class MsgType {
        MSG_CALLBACK = 0,
        MSG_RETURN = 1
    };

    int clientId;
    MsgType msgType;
    union {
        int callbackId;
        int statusCode;
    } u;

    int bufferSize;
};

class RpcClient
{
public:
    static RpcClient& Get(int port = 6969, bool isNode = false)
    {
        static RpcClient rpcClient{port, isNode};
        return rpcClient;
    }

    using Callback = std::function<void(const nlohmann::json&)>;

    // Make a call with arguments and optional callbacks
    nlohmann::json Call(
        const std::string& functionName,
        const std::vector<std::pair<std::string, nlohmann::json>>& dataArgs = {},
        const std::vector<std::pair<std::string, Callback>>& callbackArgs = {}
    );

    std::string Call(const std::string& functionName, const std::string& jsonArgs);

    void RegisterCallbackHandler(std::function<void(int, const std::string&)> fn);

    int GetClientId() { return clientId; }

private:
    RpcClient(int port, bool isNode);
    ~RpcClient();

    RpcClient(const RpcClient&) = delete;
    const RpcClient& operator=(const RpcClient&) = delete;

    nlohmann::json ProcessRPC(const RpcRequest& req);
    int RegisterCallback(Callback cb);
    
    static void ProcessCallback(
        const std::unordered_map<int, Callback>& cbRegistry,
        const ResponseHeader& respHeader,
        const std::string& respArgsJson
    );

private:
    SOCKET_TYPE clientSocket;
    int clientId;
    bool isNode;

    ResponseHeader responseHeader;
    std::string responseArgsJson;

    std::atomic_bool rpcReturnValueReady;
    std::mutex callMutex;
    std::thread receiver;
    std::atomic_bool running;
    
    std::function<void(int, const std::string&)> callbackHandler;
    std::unordered_map<int, Callback> callbackRegistry;
};

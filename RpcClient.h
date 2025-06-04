#pragma once

#include <string>
#include <functional>
#include <unordered_map>
#include <thread>

#include <nlohmann/json.hpp>

#pragma pack(push, 1)  

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
        CALLBACK = 0,
        RETURN = 1
    };

    int clientId;
    MsgType msgType;
    union {
        int callbackId;
        int statusCode;
    } u;

    int bufferSize;
};

#pragma pack(pop)

class RpcClient
{
public:
    RpcClient(int port = 6969);

    ~RpcClient();

    using Callback = std::function<void(const nlohmann::json&)>;

    // Make a call with arguments and optional callbacks
    nlohmann::json Call(
        const std::string& function_name,
        const std::initializer_list<std::pair<std::string, nlohmann::json>>& dataArgs = {},
        const std::initializer_list<std::pair<std::string, Callback>>& callbackArgs = {}
    );

    int GetClientId() { return clientId; }

private:
    nlohmann::json ProcessRPC(const RpcRequest& req);
    int RegisterCallback(Callback cb);
    
    static void ProcessCallback(
        const std::unordered_map<int, Callback>& cbRegistry,
        const ResponseHeader& respHeader,
        const std::string& respArgsJson
    );

private:
    int clientSocket;
    int clientId;

    ResponseHeader responseHeader;
    std::string responseArgsJson;

    std::atomic_bool rpcReturnValueReady;
    std::mutex callMutex;
    std::thread receiver;
    std::atomic_bool running;
    

    std::unordered_map<int, Callback> callbackRegistry;
};

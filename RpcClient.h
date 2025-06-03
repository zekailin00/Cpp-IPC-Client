#pragma once

#include <string>
#include <functional>
#include <unordered_map>

#include <nlohmann/json.hpp>

struct RpcRequest
{
    struct {
        int requestId;
        char functionName[64];
        int bufferSize;
    } header;
    std::string argsJson;
};

struct RpcResponse
{
    struct {
        int requestId;
        int statusCode;
        int bufferSize;
    } header;
    std::string argsJson;
};

struct RpcCallback
{
    struct {
        int callbackId;
        int bufferSize;
    } header;
    std::string argsJson;
};

class RpcClient
{
public:
    RpcClient(int port = 6969);

    ~RpcClient();

    using Callback = std::function<void(const nlohmann::json&)>;

    // Make a call with arguments and optional callbacks
    nlohmann::json Call(
        const std::string& function_name,
        const std::initializer_list<std::pair<std::string, nlohmann::json>>& dataArgs,
        const std::initializer_list<std::pair<std::string, Callback>>& callbackArgs = {}
    );

    int RegisterCallback(Callback cb);
    void ListenForCallbacks();

private:
    bool RequestSend(const RpcRequest& req, RpcResponse& resp);
    bool CallbackRead(RpcCallback& cb);

private:
    int clientSocket;

    std::unordered_map<int, Callback> callbackRegistry;
    int nextCallbackId = 1;
    int requestCounter = 1;
};

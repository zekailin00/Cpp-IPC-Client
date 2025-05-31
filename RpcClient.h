#ifndef RPC_CLIENT_H
#define RPC_CLIENT_H

#include <windows.h>

#include <string>
#include <functional>
#include <thread>
#include <unordered_map>

#include <nlohmann/json.hpp>

struct RpcRequest {
    int request_id;
    char function_name[64];
    char args_json[512];
};

struct RpcResponse {
    int request_id;
    int status_code;
    char result_json[512];
};

struct RpcCallback {
    int callback_id;
    char args_json[512];
};

class RpcClient {
public:
    RpcClient(const std::string& requestMap = "/MyRpcRequest",
              const std::string& responseMap = "/MyRpcResponse",
              const std::string& callbackMap = "/MyRpcCallback",
              const std::string& requestSem = "/MyRpcRequestSem",
              const std::string& responseSem = "/MyRpcResponseSem",
              const std::string& callbackSem = "/MyRpcCallbackSem");

    ~RpcClient();

    using Callback = std::function<void(const nlohmann::json&)>;

    RpcResponse send(const std::string& function, const std::string& args_json);

    // Make a call with arguments and optional callbacks
    std::string Call(
        const std::string& function_name,
        const std::initializer_list<std::pair<std::string, nlohmann::json>>& dataArgs,
        const std::initializer_list<std::pair<std::string, Callback>>& callbackArgs = {}
    );

    int RpcClient::RegisterCallback(Callback cb);
    void RpcClient::ListenForCallbacks();

private:
    HANDLE hReq, hResp, hCb;
    HANDLE hReqEvt, hRespEvt, hCbEvt;
    void* ptrReq;
    void* ptrResp;
    void* ptrCb;

    std::unordered_map<int, Callback> callbackRegistry;
    int nextCallbackId = 1;
    int requestCounter = 1;
};

#endif // RPC_CLIENT_H

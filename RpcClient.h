#ifndef RPC_CLIENT_H
#define RPC_CLIENT_H

#include <string>

#include <windows.h>

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

class RpcClient {
public:
    RpcClient(const std::string& requestMap = "/MyRpcRequest",
              const std::string& responseMap = "/MyRpcResponse",
              const std::string& requestSem = "/MyRpcRequestSem",
              const std::string& responseSem = "/MyRpcResponseSem");

    ~RpcClient();

    RpcResponse send(const std::string& function, const std::string& args_json);

private:
    HANDLE hReq, hResp;
    HANDLE hReqEvt, hRespEvt;
    void* ptrReq;
    void* ptrResp;

    int requestCounter = 1;
};

#endif // RPC_CLIENT_H

#include "RpcClient.h"
#include <cstring>
#include <stdexcept>
#include <iostream>

RpcClient::RpcClient(const std::string& requestMap,
                     const std::string& responseMap,
                     const std::string& requestSem,
                     const std::string& responseSem)
{
    hReq = OpenFileMappingA(FILE_MAP_WRITE, FALSE, requestMap.c_str());
    hResp = OpenFileMappingA(FILE_MAP_READ, FALSE, responseMap.c_str());
    hReqEvt = OpenEventA(EVENT_MODIFY_STATE, FALSE, requestSem.c_str());
    hRespEvt = OpenEventA(SYNCHRONIZE, FALSE, responseSem.c_str());

    ptrReq = MapViewOfFile(hReq, FILE_MAP_WRITE, 0, 0, sizeof(RpcRequest));
    ptrResp = MapViewOfFile(hResp, FILE_MAP_READ, 0, 0, sizeof(RpcResponse));

    if (!ptrReq || !ptrResp || !hReqEvt || !hRespEvt)
        throw std::runtime_error("Failed to initialize Windows shared memory or events");
}

RpcClient::~RpcClient()
{
    UnmapViewOfFile(ptrReq);
    UnmapViewOfFile(ptrResp);
    CloseHandle(hReq);
    CloseHandle(hResp);
    CloseHandle(hReqEvt);
    CloseHandle(hRespEvt);
}

RpcResponse RpcClient::send(const std::string& function, const std::string& args_json)
{
    RpcRequest req{};
    req.request_id = requestCounter++;
    strncpy(req.function_name, function.c_str(), sizeof(req.function_name) - 1);
    strncpy(req.args_json, args_json.c_str(), sizeof(req.args_json) - 1);

    memcpy(ptrReq, &req, sizeof(RpcRequest));

    SetEvent(hReqEvt);
    WaitForSingleObject(hRespEvt, INFINITE);

    RpcResponse resp{};
    memcpy(&resp, ptrResp, sizeof(RpcResponse));
    return resp;
}

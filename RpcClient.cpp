#include "RpcClient.h"
#include <cstring>
#include <stdexcept>
#include <iostream>

RpcClient::RpcClient(const std::string& requestMap,
                     const std::string& responseMap,
                     const std::string& callbackMap,
                     const std::string& requestSem,
                     const std::string& responseSem,
                     const std::string& callbackSem)
{
    hReq = OpenFileMappingA(FILE_MAP_WRITE, FALSE, requestMap.c_str());
    hResp = OpenFileMappingA(FILE_MAP_READ, FALSE, responseMap.c_str());
    hCb = OpenFileMappingA(FILE_MAP_READ, FALSE, callbackMap.c_str());

    hReqEvt = OpenEventA(EVENT_MODIFY_STATE, FALSE, requestSem.c_str());
    hRespEvt = OpenEventA(SYNCHRONIZE, FALSE, responseSem.c_str());
    hCbEvt = OpenEventA(SYNCHRONIZE, FALSE, callbackSem.c_str());

    ptrReq = MapViewOfFile(hReq, FILE_MAP_WRITE, 0, 0, sizeof(RpcRequest));
    ptrResp = MapViewOfFile(hResp, FILE_MAP_READ, 0, 0, sizeof(RpcResponse));
    ptrCb = MapViewOfFile(hCb, FILE_MAP_READ, 0, 0, sizeof(RpcCallback));

    if (!ptrReq || !ptrResp || !ptrCb || !hReqEvt || !hRespEvt || !hCb)
        throw std::runtime_error("Failed to initialize Windows shared memory or events");
}

RpcClient::~RpcClient()
{
    UnmapViewOfFile(ptrReq);
    UnmapViewOfFile(ptrResp);
    UnmapViewOfFile(ptrCb);
    CloseHandle(hReq);
    CloseHandle(hResp);
    CloseHandle(hCb);
    CloseHandle(hReqEvt);
    CloseHandle(hRespEvt);
    CloseHandle(hCbEvt);
}

RpcResponse RpcClient::send(const std::string& function, const std::string& args_json)
{
    RpcRequest req{};
    req.request_id = requestCounter++;
    strncpy_s(req.function_name, function.c_str(), sizeof(req.function_name) - 1);
    strncpy_s(req.args_json, args_json.c_str(), sizeof(req.args_json) - 1);

    memcpy(ptrReq, &req, sizeof(RpcRequest));

    SetEvent(hReqEvt);
    WaitForSingleObject(hRespEvt, INFINITE);

    RpcResponse resp{};
    memcpy(&resp, ptrResp, sizeof(RpcResponse));
    return resp;
}

int RpcClient::RegisterCallback(Callback cb) {
    int id = nextCallbackId++;
    callbackRegistry[id] = std::move(cb);
    return id;
}

void RpcClient::ListenForCallbacks()
{
    std::thread([=]() {
        while (true) {
            WaitForSingleObject(hCbEvt, INFINITE);
            RpcCallback cb;
            memcpy(&cb, ptrCb, sizeof(cb));
            auto it = callbackRegistry.find(cb.callback_id);

            nlohmann::json wrapped = nlohmann::json::parse(cb.args_json, nullptr, false);
            if (it != callbackRegistry.end() && !wrapped.is_discarded() && 
                wrapped.contains("keys") && wrapped.contains("values"))
            {
                nlohmann::json flat;

                const auto& keys = wrapped["keys"];
                const auto& values = wrapped["values"];
                for (size_t i = 0; i < keys.size() && i < values.size(); ++i) {
                    std::string key = keys[i];
                    std::string valStr = values[i];

                    // Try to parse value string as JSON
                    nlohmann::json parsedVal = nlohmann::json::parse(valStr, nullptr, false);
                    if (!parsedVal.is_discarded()) {
                        flat[key] = parsedVal;
                    } else {
                        flat[key] = valStr;
                    }
                }

                it->second(flat); // Invoke callback with reconstructed flat object
            }
        }
    }).detach();
}

 nlohmann::json RpcClient::Call(
    const std::string& function_name,
    const std::initializer_list<std::pair<std::string, nlohmann::json>>& dataArgs,
    const std::initializer_list<std::pair<std::string, Callback>>& callbackArgs)
{
    RpcRequest req = {requestCounter++};
    strncpy_s(req.function_name, function_name.c_str(), sizeof(req.function_name) - 1);

    std::vector<std::string> keys;
    std::vector<std::string> values;

    for (const auto& [k, v] : dataArgs) {
        keys.push_back(k);
        values.push_back(v.dump());  // Convert json to string
    }

    for (const auto& [k, cb] : callbackArgs) {
        int id = RegisterCallback(cb);
        keys.push_back(k);
        values.push_back(std::to_string(id));
    }

    nlohmann::json args;
    args["keys"] = keys;
    args["values"] = values;

    std::string args_json = args.dump();
    strncpy_s(req.args_json, args_json.c_str(), sizeof(req.args_json) - 1);

    memcpy(ptrReq, &req, sizeof(RpcRequest));
    SetEvent(hReqEvt);
    WaitForSingleObject(hRespEvt, INFINITE);

    RpcResponse resp;
    memcpy(&resp, ptrResp, sizeof(RpcResponse));
    return nlohmann::json::parse(
        (std::string)(nlohmann::json::parse(resp.result_json, nullptr, false)["result"]), 
        nullptr, false
    );
}

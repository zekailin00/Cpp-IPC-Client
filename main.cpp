#include <iostream>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <unistd.h>
#endif

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

int main() {
    RpcRequest req = {1};
    strcpy(req.function_name, "echo");
    strcpy(req.args_json, "{\"text\":3.33,\"b\":4}");

    const char* RequestMap = "/MyRpcRequest";
    const char* ResponseMap = "/MyRpcResponse";
    const char* RequestSemaphore = "/MyRpcRequestSem";
    const char* ResponseSemaphore = "/MyRpcResponseSem";

#ifdef _WIN32
    HANDLE hReq = OpenFileMappingA(FILE_MAP_WRITE, FALSE, RequestMap);
    HANDLE hResp = OpenFileMappingA(FILE_MAP_READ, FALSE, ResponseMap);
    HANDLE hReqEvt = OpenEventA(EVENT_MODIFY_STATE, FALSE, RequestSemaphore);
    HANDLE hRespEvt = OpenEventA(SYNCHRONIZE, FALSE, ResponseSemaphore);

    void* ptrReq = MapViewOfFile(hReq, FILE_MAP_WRITE, 0, 0, sizeof(RpcRequest));
    void* ptrResp = MapViewOfFile(hResp, FILE_MAP_READ, 0, 0, sizeof(RpcResponse));

    memcpy(ptrReq, &req, sizeof(RpcRequest));
    SetEvent(hReqEvt);
    WaitForSingleObject(hRespEvt, INFINITE);

    RpcResponse resp;
    memcpy(&resp, ptrResp, sizeof(RpcResponse));
    std::cout << "[C++] Response: " << resp.result_json << std::endl;

#else
    int fdReq = shm_open("/MyRpcRequest", O_RDWR | O_CREAT, 0666);
    ftruncate(fdReq, sizeof(RpcRequest));
    void* ptrReq = mmap(nullptr, sizeof(RpcRequest), PROT_WRITE, MAP_SHARED, fdReq, 0);
    memcpy(ptrReq, &req, sizeof(RpcRequest));

    int fdResp = shm_open("/MyRpcResponse", O_RDWR | O_CREAT, 0666);
    ftruncate(fdResp, sizeof(RpcResponse));
    void* ptrResp = mmap(nullptr, sizeof(RpcResponse), PROT_READ, MAP_SHARED, fdResp, 0);

    sem_t* semReq = sem_open("/MyRpcRequestSem", O_CREAT, 0666, 0);
    sem_t* semResp = sem_open("/MyRpcResponseSem", O_CREAT, 0666, 0);

    sem_post(semReq);
    sem_wait(semResp);

    RpcResponse resp;
    memcpy(&resp, ptrResp, sizeof(RpcResponse));
    std::cout << "[C++] Response: " << resp.result_json << std::endl;
#endif

    return 0;
}

#include "RpcClient.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <thread>
#include <iostream>

#define SOCKET_CHECK(status) if (status < 0)    \
    {                                           \
        perror("Failed: " #status "\n");        \
        exit(EXIT_FAILURE);                     \
    }

#define STATUS_CHECK(status, msg) if (status)   \
    {                                           \
        perror("ERROR: " #msg "\n");            \
        exit(EXIT_FAILURE);                     \
    }

RpcClient::RpcClient(int port)
{
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = INADDR_ANY;

    SOCKET_CHECK((clientSocket = socket(AF_INET, SOCK_STREAM, 0)) < 0);
    SOCKET_CHECK((connect(clientSocket, (struct sockaddr*)&address, sizeof(address))) < 0);
}

RpcClient::~RpcClient()
{
    close(clientSocket);
}

bool RpcClient::RequestSend(const RpcRequest& req, RpcResponse& resp)
{
    size_t sizeSent = send(clientSocket, &req.header, sizeof(req.header), 0);
    STATUS_CHECK(sizeSent != sizeof(req.header), "DEBUG: failed to send header");

    if (req.header.bufferSize != 0)
    {
        size_t index = 0;
        while (index < req.header.bufferSize)
        {
            size_t chunk_size = std::min(1024ul, req.header.bufferSize - index);
            const char* data_ptr = req.argsJson.c_str() + index;
            ssize_t bytes_sent = send(clientSocket, data_ptr, chunk_size, 0);

            STATUS_CHECK(bytes_sent == -1, "DEBUG: failed to send everything");
            index += bytes_sent;
        }
    }

    size_t sizeRecv = recv(clientSocket, &resp.header, sizeof(resp.header), 0);
    STATUS_CHECK(sizeRecv != sizeof(resp.header), "DEBUG: failed to recv header");

    if (resp.header.bufferSize != 0)
    {
        resp.argsJson.resize(resp.header.bufferSize);
    
        size_t bytesReceived = 0;
        while (bytesReceived < resp.header.bufferSize)
        {
            size_t chunkSize = std::min(1024ul, resp.header.bufferSize - bytesReceived);
            char* dataPtr = resp.argsJson.data() + bytesReceived;
            ssize_t chunkBytesReceived = recv(clientSocket, dataPtr, chunkSize, 0);

            STATUS_CHECK(chunkBytesReceived == -1, "Error receiving message data");
            bytesReceived += chunkBytesReceived;
        }
    }

    return true;
}

bool RpcClient::CallbackRead(RpcCallback& cb)
{
    size_t sizeRecv = recv(clientSocket, &cb.header, sizeof(cb.header), 0);
    STATUS_CHECK(sizeRecv != sizeof(cb.header), "DEBUG: failed to recv header");

    if (cb.header.bufferSize != 0)
    {
        cb.argsJson.resize(cb.header.bufferSize);
    
        size_t bytesReceived = 0;
        while (bytesReceived < cb.header.bufferSize)
        {
            size_t chunkSize = std::min(1024ul, cb.header.bufferSize - bytesReceived);
            char* dataPtr = cb.argsJson.data() + bytesReceived;
            ssize_t chunkBytesReceived = recv(clientSocket, dataPtr, chunkSize, 0);

            STATUS_CHECK(chunkBytesReceived == -1, "Error receiving message data");
            bytesReceived += chunkBytesReceived;
        }
    }
    return true;
}

int RpcClient::RegisterCallback(Callback cb)
{
    int id = nextCallbackId++;
    callbackRegistry[id] = std::move(cb);
    return id;
}

void RpcClient::ListenForCallbacks()
{
    std::thread([=]() {
        while (true) {
            RpcCallback cb;
            if (!CallbackRead(cb))
                continue;

            auto it = callbackRegistry.find(cb.header.callbackId);

            nlohmann::json wrapped = nlohmann::json::parse(cb.argsJson, nullptr, false);
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
    const std::string& functionName,
    const std::initializer_list<std::pair<std::string, nlohmann::json>>& dataArgs,
    const std::initializer_list<std::pair<std::string, Callback>>& callbackArgs)
{
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

    RpcRequest req = {requestCounter++};
    strncpy(
        req.header.functionName,
        functionName.c_str(),
        sizeof(req.header.functionName) - 1
    );
    req.header.bufferSize = args.size();
    req.argsJson = args.dump();

    RpcResponse resp;
    RequestSend(req, resp);

    return nlohmann::json::parse(
        (std::string)(nlohmann::json::parse(resp.argsJson, nullptr, false)["result"]), 
        nullptr, false
    );
}

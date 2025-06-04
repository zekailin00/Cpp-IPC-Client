#include "RpcClient.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

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

    size_t sizeRecv = recv(clientSocket, &responseHeader, sizeof(ResponseHeader), 0);
    STATUS_CHECK(
        sizeRecv != sizeof(ResponseHeader) || responseHeader.bufferSize != 0, 
        "DEBUG: Error receiving client ID."
    );

    rpcReturnValueReady.store(false);
    running.store(true);
    clientId = responseHeader.clientId;
    printf("Client ID: %d\n", clientId);

    receiver = std::thread([&]()
    {
        while (running)
        {
            while (rpcReturnValueReady == true)
                continue;

            size_t sizeRecv = recv(clientSocket, &responseHeader, sizeof(ResponseHeader), MSG_WAITALL);
            STATUS_CHECK(sizeRecv != sizeof(ResponseHeader) && running, "DEBUG: failed to recv callback header");
            
            if (!running)
                return;
        
            if (responseHeader.bufferSize != 0)
            {
                responseArgsJson.resize(responseHeader.bufferSize);
            
                size_t bytesReceived = 0;
                while (bytesReceived < responseHeader.bufferSize)
                {
                    size_t chunkSize = std::min(1024ul, responseHeader.bufferSize - bytesReceived);
                    char* dataPtr = responseArgsJson.data() + bytesReceived;
                    ssize_t chunkBytesReceived = recv(clientSocket, dataPtr, chunkSize, 0);
        
                    STATUS_CHECK(chunkBytesReceived == -1, "Error receiving message data");
                    bytesReceived += chunkBytesReceived;
                }
            }

            if (responseHeader.msgType == ResponseHeader::MsgType::CALLBACK)
            {
                std::thread(ProcessCallback, callbackRegistry, responseHeader, responseArgsJson).detach();
            }
            else if (responseHeader.msgType == ResponseHeader::MsgType::RETURN)
            {                
                rpcReturnValueReady = true;
            }
            else
            {
                std::runtime_error("[RPC Client] ERROR: Corrupted response header.");
            }
        }
    });
}

RpcClient::~RpcClient()
{
    running.store(false);
    close(clientSocket);
    receiver.join();
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

    RpcRequest rpcRequest;
    rpcRequest.header.clientId = clientId;
    strncpy(
        rpcRequest.header.functionName,
        functionName.c_str(),
        sizeof(rpcRequest.header.functionName) - 1
    );
    rpcRequest.jsonArgs = args.dump();
    rpcRequest.header.bufferSize = rpcRequest.jsonArgs.size();

    return ProcessRPC(rpcRequest);
}

int RpcClient::RegisterCallback(Callback cb)
{
    int id = Call("_RPC::AllocateCallback", {{"clientId", clientId}});
    callbackRegistry[id] = std::move(cb);
    return id;
}

void RpcClient::ProcessCallback(
    const std::unordered_map<int, Callback>& cbRegistry,
    const ResponseHeader& respHeader,
    const std::string& respArgsJson)
{
    auto it = cbRegistry.find(respHeader.u.callbackId);

    nlohmann::json wrapped = nlohmann::json::parse(respArgsJson, nullptr, false);
    if (it != cbRegistry.end() && !wrapped.is_discarded() && 
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

nlohmann::json RpcClient::ProcessRPC(const RpcRequest& req)
{
    {
        std::lock_guard<std::mutex> RpcLock(callMutex);
        
        size_t sizeSent = send(clientSocket, &req, sizeof(req.header), 0);
        STATUS_CHECK(sizeSent != sizeof(req.header), "DEBUG: failed to send header");

        if (req.header.bufferSize != 0)
        {
            size_t index = 0;
            while (index < req.header.bufferSize)
            {
                size_t chunk_size = std::min(1024ul, req.header.bufferSize - index);
                const char* data_ptr = req.jsonArgs.c_str() + index;
                ssize_t bytes_sent = send(clientSocket, data_ptr, chunk_size, 0);

                STATUS_CHECK(bytes_sent == -1, "DEBUG: failed to send everything");
                index += bytes_sent;
            }
        }
    }
    
    while (rpcReturnValueReady == false)
        continue;

    printf("responseArgsJson:%s\n", responseArgsJson.c_str());
    auto str = nlohmann::json::parse(responseArgsJson)["result"].get<std::string>();
    auto retval = nlohmann::json::parse(str, nullptr, false);
    rpcReturnValueReady = false;

    if (retval.is_discarded()) {
        return str;
    }
    return retval;
}

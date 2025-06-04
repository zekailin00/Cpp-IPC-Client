#include "RpcClient.h"

#ifdef _WIN32
#include <algorithm>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

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
#ifdef _WIN32
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != 0) {
        throw std::runtime_error("WSAStartup failed.\n");
    }
    clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == INVALID_SOCKET) {
        WSACleanup();
        throw std::runtime_error("Error at socket creation.\n");
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(clientSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        throw std::runtime_error("Socket connection failed.\n");
    }
#else
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = INADDR_ANY;

    SOCKET_CHECK((clientSocket = socket(AF_INET, SOCK_STREAM, 0)) < 0);
    SOCKET_CHECK((connect(clientSocket, (struct sockaddr*)&address, sizeof(address))) < 0);
#endif

    size_t sizeRecv = recv(clientSocket, (char*)&responseHeader, sizeof(ResponseHeader), 0);
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

            size_t sizeRecv = recv(clientSocket, (char*)&responseHeader, sizeof(ResponseHeader), MSG_WAITALL);
            STATUS_CHECK(sizeRecv != sizeof(ResponseHeader) && running, "DEBUG: failed to recv callback header");
            
            if (!running)
                return;
        
            if (responseHeader.bufferSize != 0)
            {
                responseArgsJson.resize(responseHeader.bufferSize);
            
                int bytesReceived = 0;
                while (bytesReceived < responseHeader.bufferSize)
                {
                    int chunkSize = std::min<int>(1024, responseHeader.bufferSize - bytesReceived);
                    char* dataPtr = responseArgsJson.data() + bytesReceived;
                    int chunkBytesReceived = recv(clientSocket, dataPtr, chunkSize, 0);
        
                    STATUS_CHECK(chunkBytesReceived == -1, "Error receiving message data");
                    bytesReceived += chunkBytesReceived;
                }
            }

            if (responseHeader.msgType == ResponseHeader::MsgType::MSG_CALLBACK)
            {
                std::thread(ProcessCallback, callbackRegistry, responseHeader, responseArgsJson).detach();
            }
            else if (responseHeader.msgType == ResponseHeader::MsgType::MSG_RETURN)
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
#ifdef _WIN32
    closesocket(clientSocket);
    WSACleanup();
#else
    close(clientSocket);
#endif
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
        
        size_t sizeSent = send(clientSocket, (char*)&req, sizeof(req.header), 0);
        STATUS_CHECK(sizeSent != sizeof(req.header), "DEBUG: failed to send header");

        if (req.header.bufferSize != 0)
        {
            int index = 0;
            while (index < req.header.bufferSize)
            {
                int chunk_size = std::min<int>(1024, req.header.bufferSize - index);
                const char* data_ptr = req.jsonArgs.c_str() + index;
                int bytes_sent = send(clientSocket, data_ptr, chunk_size, 0);

                STATUS_CHECK(bytes_sent == -1, "DEBUG: failed to send everything");
                index += bytes_sent;
            }
        }
    }
    
    while (rpcReturnValueReady == false)
        continue;

    printf("RPC: %s |-> %s\n", req.header.functionName, responseArgsJson.c_str());
    auto str = nlohmann::json::parse(responseArgsJson)["result"].get<std::string>();
    auto retval = nlohmann::json::parse(str, nullptr, false);
    rpcReturnValueReady = false;

    if (retval.is_discarded()) {
        return str;
    }
    return retval;
}

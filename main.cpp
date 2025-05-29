
#include "RpcClient.h"
#include <iostream>

int main() {
    RpcClient rpcClient{};

    RpcResponse resp;
    
    resp = rpcClient.send
    ("sub", 
        "{\"keys\": [\"a\", \"b\"], "
        "\"values\": [\"5\", \"10\"]}"
    );
    std::cout << "[C++] Response: " << resp.result_json << std::endl;

    resp = rpcClient.send
    ("add", 
        "{\"keys\": [\"a\", \"b\"], "
        "\"values\": [\"5\", \"123.2\"]}"
    );
    std::cout << "[C++] Response: " << resp.result_json << std::endl;

    resp = rpcClient.send
    ("echo", 
        "{\"keys\": [\"text\", \"b\"], "
        "\"values\": [\"c\", \"10\"]}"
    );
    std::cout << "[C++] Response: " << resp.result_json << std::endl;
    return 0;
}


#include "RpcClient.h"
#include <iostream>

int main() {
    RpcClient rpcClient{};

    RpcResponse resp = rpcClient.send("sub", "{\"d\":3.33,\"b\":4}");
    std::cout << "[C++] Response: " << resp.result_json << std::endl;
    return 0;
}

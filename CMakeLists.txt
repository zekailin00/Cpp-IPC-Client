set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


add_library(rpcClient RpcClient.cpp)
target_include_directories(rpcClient PUBLIC include .)

add_executable(rpcMain main.cpp)
target_link_libraries(rpcMain rpcClient)
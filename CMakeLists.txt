set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(main main.cpp RpcClient.cpp)
target_include_directories(main PRIVATE .)
#include <windows.h>
#include <iostream>
#include <cstring>

struct Message {
    int command_id;
    char payload[256];
};

int main() {
    const char* map_name = "Local\\MySharedMemory";
    const char* event_name = "Local\\MySharedMemoryEvent";

    HANDLE hMap = OpenFileMappingA(FILE_MAP_WRITE, FALSE, map_name);
    if (!hMap) {
        std::cerr << "Failed to open file mapping.\n";
        return 1;
    }

    HANDLE hEvent = OpenEventA(EVENT_MODIFY_STATE, FALSE, event_name);
    if (!hEvent) {
        std::cerr << "Failed to open event.\n";
        return 1;
    }

    void* pBuf = MapViewOfFile(hMap, FILE_MAP_WRITE, 0, 0, sizeof(Message));
    if (!pBuf) {
        std::cerr << "Failed to map view.\n";
        return 1;
    }

    Message msg;
    msg.command_id = 1;
    strcpy_s(msg.payload, "Hello from C++");

    memcpy(pBuf, &msg, sizeof(msg));
    SetEvent(hEvent);

    std::cout << "Sent message to C# server.\n";

    UnmapViewOfFile(pBuf);
    CloseHandle(hMap);
    CloseHandle(hEvent);
    return 0;
}

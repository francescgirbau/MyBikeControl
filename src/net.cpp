#include "mybc/net.h"

#if defined(_WIN32)
#include <winsock2.h>

#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <unistd.h>
#endif

namespace mybc::net {

#if defined(_WIN32)

bool initialize() {
    WSADATA data{};
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

void cleanup() { WSACleanup(); }

void closeSocket(Socket socket) {
    if (socket != kInvalidSocket) {
        closesocket(static_cast<SOCKET>(socket));
    }
}

int lastError() { return WSAGetLastError(); }

std::string errorString(int code) {
    char* buffer = nullptr;
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(code), 0, reinterpret_cast<char*>(&buffer), 0, nullptr);

    std::string message;
    if (length != 0 && buffer != nullptr) {
        message.assign(buffer, length);
        LocalFree(buffer);
    } else {
        message = "error " + std::to_string(code);
    }

    while (!message.empty() &&
           (message.back() == '\r' || message.back() == '\n' || message.back() == ' ')) {
        message.pop_back();
    }
    return message;
}

#else

bool initialize() { return true; }

void cleanup() {}

void closeSocket(Socket socket) {
    if (socket != kInvalidSocket) {
        ::close(socket);
    }
}

int lastError() { return errno; }

std::string errorString(int code) { return std::strerror(code); }

#endif

}  // namespace mybc::net

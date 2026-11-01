#pragma once

#include <cstdint>
#include <string>

namespace mybc::net {

#if defined(_WIN32)
/// Winsock sockets are pointer-sized handles, not ints.
using Socket = std::uintptr_t;
inline constexpr Socket kInvalidSocket = static_cast<Socket>(~static_cast<Socket>(0));
#else
using Socket = int;
inline constexpr Socket kInvalidSocket = -1;
#endif

/// Initializes the platform socket layer (WSAStartup on Windows).
bool initialize();

/// Releases the platform socket layer (WSACleanup on Windows).
void cleanup();

/// Closes a socket created by the platform layer.
void closeSocket(Socket socket);

/// Returns the last socket error for the calling thread.
int lastError();

/// Human-readable description of a socket error code.
std::string errorString(int code);

}  // namespace mybc::net

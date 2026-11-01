#include "mybc/obc/server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <utility>
#include <vector>

#include "mybc/log.h"

namespace mybc::obc {
namespace {

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

void configureClientSocket(int fd) {
    int yes = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
#ifdef SO_NOSIGPIPE
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
}

}  // namespace

struct Server::Shared {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<int> clients;
    std::atomic<bool> running{false};

    AppInfoCallback onAppInfo;
    HapticCallback onHaptic;
    ClientCallback onClient;
};

Server::Server(DeviceInfo info, uint16_t port) : info_(std::move(info)), port_(port) {}

Server::~Server() { stop(); }

bool Server::start() {
    if (running_) {
        return false;
    }

    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        logError(std::string("socket() failed: ") + std::strerror(errno));
        return false;
    }

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port_);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        logError("bind() failed on port " + std::to_string(port_) + ": " + std::strerror(errno));
        ::close(fd);
        return false;
    }

    if (::listen(fd, 4) < 0) {
        logError(std::string("listen() failed: ") + std::strerror(errno));
        ::close(fd);
        return false;
    }

    sockaddr_in bound{};
    socklen_t boundLength = sizeof(bound);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &boundLength) == 0) {
        port_ = ntohs(bound.sin_port);
    }

    listenFd_ = fd;
    shared_ = std::make_shared<Shared>();
    shared_->running = true;
    shared_->onAppInfo = onAppInfo_;
    shared_->onHaptic = onHaptic_;
    shared_->onClient = onClient_;
    running_ = true;
    acceptThread_ = std::thread(&Server::acceptLoop, this);

    logInfo("TCP server listening on 0.0.0.0:" + std::to_string(port_));
    return true;
}

void Server::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    if (shared_) {
        shared_->running = false;
    }

    // The accept loop wakes up on its 200 ms select timeout; joining before
    // closing avoids closing an fd that select() is still watching.
    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }

    if (listenFd_ >= 0) {
        ::close(listenFd_);
        listenFd_ = -1;
    }

    if (shared_) {
        std::unique_lock<std::mutex> lock(shared_->mutex);
        for (const int fd : shared_->clients) {
            ::shutdown(fd, SHUT_RDWR);
        }
        shared_->cv.wait_for(lock, std::chrono::milliseconds(500),
                             [this] { return shared_->clients.empty(); });
    }
}

size_t Server::clientCount() const {
    if (!shared_) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(shared_->mutex);
    return shared_->clients.size();
}

bool Server::sendButton(uint8_t buttonId, uint8_t state) {
    const Bytes message = encodeButtonState({buttonId, state});
    return sendAll(message.data(), message.size());
}

bool Server::sendButtonState(const Bytes& pairs) {
    const Bytes message = encodeButtonState(pairs);
    if (message.empty()) {
        return false;
    }
    return sendAll(message.data(), message.size());
}

bool Server::sendDeviceStatus() {
    const Bytes message = encodeDeviceStatus(info_.batteryPercent, true);
    return sendAll(message.data(), message.size());
}

void Server::setOnAppInfo(AppInfoCallback callback) {
    onAppInfo_ = std::move(callback);
    if (shared_) {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        shared_->onAppInfo = onAppInfo_;
    }
}

void Server::setOnHaptic(HapticCallback callback) {
    onHaptic_ = std::move(callback);
    if (shared_) {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        shared_->onHaptic = onHaptic_;
    }
}

void Server::setOnClientConnected(ClientCallback callback) {
    onClient_ = std::move(callback);
    if (shared_) {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        shared_->onClient = onClient_;
    }
}

void Server::acceptLoop() {
    while (shared_->running) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenFd_, &readSet);

        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 200 * 1000;

        const int ready = ::select(listenFd_ + 1, &readSet, nullptr, nullptr, &timeout);
        if (ready <= 0) {
            continue;
        }

        const int clientFd = ::accept(listenFd_, nullptr, nullptr);
        if (clientFd < 0) {
            continue;
        }

        configureClientSocket(clientFd);
        {
            std::lock_guard<std::mutex> lock(shared_->mutex);
            shared_->clients.push_back(clientFd);
        }

        logInfo("app connected");
        ClientCallback onClient;
        {
            std::lock_guard<std::mutex> lock(shared_->mutex);
            onClient = shared_->onClient;
        }
        if (onClient) {
            onClient();
        }
        sendDeviceStatus();

        std::thread(&Server::clientLoop, shared_, clientFd).detach();
    }
}

void Server::clientLoop(std::shared_ptr<Shared> shared, int fd) {
    StreamParser parser;
    uint8_t buffer[512];

    while (shared->running) {
        const ssize_t received = ::recv(fd, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            break;
        }

        for (const IncomingMessage& message : parser.feed(buffer, static_cast<size_t>(received))) {
            switch (message.kind) {
                case IncomingKind::AppInfo: {
                    logInfo("app info: " + message.appInfo.appId + " v" +
                            message.appInfo.appVersion + " (" +
                            std::to_string(message.appInfo.supportedButtons.size()) + " buttons)");
                    AppInfoCallback onAppInfo;
                    {
                        std::lock_guard<std::mutex> lock(shared->mutex);
                        onAppInfo = shared->onAppInfo;
                    }
                    if (onAppInfo) {
                        onAppInfo(message.appInfo);
                    }
                    break;
                }

                case IncomingKind::HapticFeedback: {
                    logInfo("haptic: pattern=" + std::to_string(message.haptic.pattern) +
                            " duration=" + std::to_string(message.haptic.duration) +
                            " intensity=" + std::to_string(message.haptic.intensity));
                    HapticCallback onHaptic;
                    {
                        std::lock_guard<std::mutex> lock(shared->mutex);
                        onHaptic = shared->onHaptic;
                    }
                    if (onHaptic) {
                        onHaptic(message.haptic);
                    }
                    break;
                }

                case IncomingKind::Unknown:
                    logWarn("unknown message type 0x" + toHex(&message.rawType, 1));
                    break;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        const auto it = std::find(shared->clients.begin(), shared->clients.end(), fd);
        if (it != shared->clients.end()) {
            shared->clients.erase(it);
        }
    }
    shared->cv.notify_all();

    ::close(fd);
    logInfo("app disconnected");
}

bool Server::sendAll(const uint8_t* data, size_t length) {
    if (!shared_) {
        return false;
    }

    std::lock_guard<std::mutex> lock(shared_->mutex);
    bool delivered = false;

    for (const int fd : shared_->clients) {
        const ssize_t sent = ::send(fd, data, length, kSendFlags);
        if (sent == static_cast<ssize_t>(length)) {
            delivered = true;
        } else {
            // Let the client thread notice and clean up.
            ::shutdown(fd, SHUT_RDWR);
        }
    }

    return delivered;
}

}  // namespace mybc::obc

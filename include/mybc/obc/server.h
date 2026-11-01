#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "mybc/obc/protocol.h"

namespace mybc::obc {

struct DeviceInfo {
    std::string name = "MyBikeControl";
    std::string id = "mybc-0001";
    std::string manufacturer = "MyBikeControl";
    std::string model = "Virtual Controller";
    uint8_t batteryPercent = 0xFF;
};

/// TCP server implementing the device side of the OpenBikeControl mDNS transport.
///
/// One TCP connection per app. Messages are written with a single send() call
/// and TCP_NODELAY, matching the reference implementation in the protocol repo.
class Server {
  public:
    using AppInfoCallback = std::function<void(const AppInfo&)>;
    using HapticCallback = std::function<void(const HapticCommand&)>;
    using ClientCallback = std::function<void()>;

    Server(DeviceInfo info, uint16_t port);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    /// Binds, listens and starts the accept thread. Port 0 picks a free port.
    bool start();
    void stop();

    uint16_t port() const { return port_; }
    size_t clientCount() const;

    bool sendButton(uint8_t buttonId, uint8_t state);
    bool sendButtonState(const Bytes& pairs);
    bool sendDeviceStatus();

    void setOnAppInfo(AppInfoCallback callback);
    void setOnHaptic(HapticCallback callback);
    void setOnClientConnected(ClientCallback callback);

  private:
    struct Shared;

    void acceptLoop();
    static void clientLoop(std::shared_ptr<Shared> shared, int fd);
    bool sendAll(const uint8_t* data, size_t length);

    DeviceInfo info_;
    uint16_t port_;
    int listenFd_ = -1;
    std::atomic<bool> running_{false};
    std::thread acceptThread_;
    std::shared_ptr<Shared> shared_;

    AppInfoCallback onAppInfo_;
    HapticCallback onHaptic_;
    ClientCallback onClient_;
};

}  // namespace mybc::obc

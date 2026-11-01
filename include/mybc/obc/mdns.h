#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "mybc/obc/server.h"

namespace mybc::obc {

/// Advertises the bridge as an OpenBikeControl device over mDNS/DNS-SD.
///
/// Windows uses the native DNS-SD API (`DnsServiceRegister`); macOS uses
/// `dns_sd`. Other platforms are a no-op stub so the TCP transport can still
/// be exercised with a manual connection.
class MdnsAdvertiser {
  public:
    MdnsAdvertiser(std::string serviceName, uint16_t port, DeviceInfo deviceInfo);
    ~MdnsAdvertiser();

    MdnsAdvertiser(const MdnsAdvertiser&) = delete;
    MdnsAdvertiser& operator=(const MdnsAdvertiser&) = delete;

    bool start();
    void stop();

  private:
    struct Impl;

    std::string serviceName_;
    uint16_t port_;
    DeviceInfo deviceInfo_;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mybc::obc

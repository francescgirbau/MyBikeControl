#include "mybc/obc/mdns.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <thread>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <arpa/inet.h>
#include <dns_sd.h>
#include <sys/select.h>
#include <sys/time.h>
#endif

#include "mybc/log.h"

namespace mybc::obc {
namespace {

#if defined(__APPLE__)
constexpr const char* kServiceType = "_openbikecontrol._tcp";
constexpr const char* kServiceUuid = "d273f680-d548-419d-b9d1-fa0472345229";

std::vector<uint8_t> buildTxtRecord(const DeviceInfo& info) {
    std::vector<uint8_t> txt;

    const auto add = [&txt](const std::string& entry) {
        const size_t length = std::min<size_t>(entry.size(), 255);
        txt.push_back(static_cast<uint8_t>(length));
        txt.insert(txt.end(), entry.begin(),
                   entry.begin() + static_cast<std::ptrdiff_t>(length));
    };

    add("version=1");
    add("id=" + info.id);
    add("name=" + info.name);
    add("service-uuids=" + std::string(kServiceUuid));
    add("manufacturer=" + info.manufacturer);
    add("model=" + info.model);

    return txt;
}
#endif

}  // namespace

struct MdnsAdvertiser::Impl {
#if defined(__APPLE__)
    DNSServiceRef ref = nullptr;
    std::thread thread;
    std::atomic<bool> running{false};
#endif
};

MdnsAdvertiser::MdnsAdvertiser(std::string serviceName, uint16_t port, DeviceInfo deviceInfo)
    : serviceName_(std::move(serviceName)),
      port_(port),
      deviceInfo_(std::move(deviceInfo)),
      impl_(std::make_unique<Impl>()) {}

MdnsAdvertiser::~MdnsAdvertiser() { stop(); }

bool MdnsAdvertiser::start() {
#if defined(__APPLE__)
    if (impl_->ref != nullptr) {
        return false;
    }

    const std::vector<uint8_t> txt = buildTxtRecord(deviceInfo_);
    const DNSServiceErrorType error = DNSServiceRegister(
        &impl_->ref, 0, kDNSServiceInterfaceIndexAny, serviceName_.c_str(), kServiceType,
        nullptr, nullptr, htons(port_), static_cast<uint16_t>(txt.size()), txt.data(), nullptr,
        nullptr);

    if (error != kDNSServiceErr_NoError) {
        logError("DNSServiceRegister failed with code " + std::to_string(error));
        impl_->ref = nullptr;
        return false;
    }

    impl_->running = true;
    impl_->thread = std::thread([this] {
        const int fd = DNSServiceRefSockFD(impl_->ref);
        if (fd < 0) {
            return;
        }

        while (impl_->running) {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(fd, &readSet);

            timeval timeout{};
            timeout.tv_sec = 0;
            timeout.tv_usec = 200 * 1000;

            if (::select(fd + 1, &readSet, nullptr, nullptr, &timeout) > 0 &&
                FD_ISSET(fd, &readSet)) {
                DNSServiceProcessResult(impl_->ref);
            }
        }
    });

    logInfo("mDNS advertising as \"" + serviceName_ + "." + kServiceType + ".local.\" on port " +
            std::to_string(port_));
    return true;
#else
    logWarn(std::string("mDNS advertising is only implemented on macOS in this build; "
                        "the TCP server is still running on port ") +
            std::to_string(port_));
    return false;
#endif
}

void MdnsAdvertiser::stop() {
#if defined(__APPLE__)
    if (!impl_->running.exchange(false)) {
        return;
    }

    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }

    if (impl_->ref != nullptr) {
        DNSServiceRefDeallocate(impl_->ref);
        impl_->ref = nullptr;
    }
#endif
}

}  // namespace mybc::obc

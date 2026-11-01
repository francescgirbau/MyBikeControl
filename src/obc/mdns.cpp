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
#elif defined(_WIN32)
#include <winsock2.h>

#include <windows.h>
#include <windns.h>
#include <winerror.h>
#endif

#include "mybc/log.h"

#if defined(_WIN32) && !defined(DNS_REQUEST_PENDING)
// Present in the Windows SDK; older MinGW-w64 headers omit it.
#define DNS_REQUEST_PENDING 9506L
#endif

namespace mybc::obc {
namespace {

constexpr const char* kServiceType = "_openbikecontrol._tcp";
constexpr const char* kServiceUuid = "d273f680-d548-419d-b9d1-fa0472345229";

#if defined(__APPLE__)

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

#elif defined(_WIN32)

std::wstring widen(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const int length = MultiByteToWideChar(CP_UTF8, 0, value.c_str(),
                                           static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) {
        return {};
    }

    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                        result.data(), length);
    return result;
}

VOID WINAPI registerComplete(DWORD status, PVOID, PDNS_SERVICE_INSTANCE) {
    // The same callback is used for registration and deregistration, so only
    // report failures here. Success is logged by start().
    if (status != ERROR_SUCCESS && status != ERROR_CANCELLED) {
        logWarn("mDNS registration callback returned " + std::to_string(status));
    }
}

#endif

}  // namespace

struct MdnsAdvertiser::Impl {
#if defined(__APPLE__)
    DNSServiceRef ref = nullptr;
    std::thread thread;
    std::atomic<bool> running{false};
#elif defined(_WIN32)
    DNS_SERVICE_REGISTER_REQUEST request{};
    PDNS_SERVICE_INSTANCE instance = nullptr;
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
#elif defined(_WIN32)
    if (impl_->instance != nullptr) {
        return false;
    }

    const std::wstring instanceName =
        widen(serviceName_) + L"." + widen(kServiceType) + L".local";

    wchar_t hostBuffer[256] = {};
    DWORD hostLength = static_cast<DWORD>(sizeof(hostBuffer) / sizeof(hostBuffer[0]));
    std::wstring hostName;
    if (GetComputerNameExW(ComputerNameDnsHostname, hostBuffer, &hostLength)) {
        hostName = hostBuffer;
        hostName += L".local";
    }

    const std::vector<std::wstring> keys = {
        L"version", L"id", L"name", L"service-uuids", L"manufacturer", L"model"};
    const std::vector<std::wstring> values = {
        L"1",
        widen(deviceInfo_.id),
        widen(deviceInfo_.name),
        widen(kServiceUuid),
        widen(deviceInfo_.manufacturer),
        widen(deviceInfo_.model),
    };

    std::vector<PCWSTR> keyPointers;
    std::vector<PCWSTR> valuePointers;
    keyPointers.reserve(keys.size());
    valuePointers.reserve(values.size());
    for (const std::wstring& key : keys) {
        keyPointers.push_back(key.c_str());
    }
    for (const std::wstring& value : values) {
        valuePointers.push_back(value.c_str());
    }

    impl_->instance = DnsServiceConstructInstance(
        instanceName.c_str(), hostName.empty() ? nullptr : hostName.c_str(), nullptr, nullptr,
        port_, 0, 0, static_cast<DWORD>(keys.size()), keyPointers.data(),
        valuePointers.data());

    if (impl_->instance == nullptr) {
        logError("DnsServiceConstructInstance failed");
        return false;
    }

    impl_->request = {};
    impl_->request.Version = DNS_QUERY_REQUEST_VERSION1;
    impl_->request.InterfaceIndex = 0;
    impl_->request.pServiceInstance = impl_->instance;
    impl_->request.pRegisterCompletionCallback = &registerComplete;
    impl_->request.pQueryContext = nullptr;
    impl_->request.hCredentials = nullptr;
    impl_->request.unicastEnabled = FALSE;

    const DWORD result = DnsServiceRegister(&impl_->request, nullptr);
    if (result != DNS_REQUEST_PENDING && result != ERROR_SUCCESS) {
        logError("DnsServiceRegister failed with code " + std::to_string(result));
        DnsServiceFreeInstance(impl_->instance);
        impl_->instance = nullptr;
        return false;
    }

    logInfo("mDNS advertising as \"" + serviceName_ + "." + kServiceType + ".local.\" on port " +
            std::to_string(port_));
    return true;
#else
    logWarn(std::string("mDNS advertising is not implemented on this platform; "
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
#elif defined(_WIN32)
    if (impl_->instance == nullptr) {
        return;
    }

    DnsServiceDeRegister(&impl_->request, nullptr);
    DnsServiceFreeInstance(impl_->instance);
    impl_->instance = nullptr;
#endif
}

}  // namespace mybc::obc

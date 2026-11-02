#include "mybc/ble/ride_bridge.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "mybc/log.h"

#if MYBC_WITH_BLE
#include <simpleble/SimpleBLE.h>
#endif

namespace mybc::ble {
namespace {

constexpr const char* kServiceUuid = "0000fc82-0000-1000-8000-00805f9b34fb";
constexpr const char* kAsyncCharacteristicUuid = "00000002-19ca-4651-86e5-fa29dcdd09d1";
constexpr const char* kSyncRxCharacteristicUuid = "00000003-19ca-4651-86e5-fa29dcdd09d1";
constexpr const char* kSyncTxCharacteristicUuid = "00000004-19ca-4651-86e5-fa29dcdd09d1";
constexpr const char* kDeviceInfoServiceUuid = "0000180a-0000-1000-8000-00805f9b34fb";
constexpr const char* kSerialCharacteristicUuid = "00002a25-0000-1000-8000-00805f9b34fb";

constexpr uint8_t kBatteryNotificationType = 0x19;
constexpr uint8_t kRideOn[] = {0x52, 0x69, 0x64, 0x65, 0x4f, 0x6e};  // "RideOn"

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return toLower(haystack).find(toLower(needle)) != std::string::npos;
}

bool waitWhile(const std::atomic<bool>& running, int milliseconds) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (running && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return running;
}

}  // namespace

#if MYBC_WITH_BLE

struct RideBridge::Impl {
    struct Shared {
        std::atomic<bool> running{false};
        std::mutex mutex;
        std::map<std::string, RideDecoder> devices;
        std::set<std::string> activeDevices;
        std::set<uint8_t> globalButtons;

        RideBridgeOptions options;
        EventCallback onEvents;
        BatteryCallback onBattery;
    };

    Impl(RideBridgeOptions options, EventCallback onEvents, BatteryCallback onBattery)
        : shared_(std::make_shared<Shared>()) {
        shared_->options = std::move(options);
        shared_->onEvents = std::move(onEvents);
        shared_->onBattery = std::move(onBattery);
    }

    bool start() {
        if (shared_->running.exchange(true)) {
            return false;
        }

        try {
            if (SimpleBLE::Adapter::get_adapters().empty()) {
                logError("No Bluetooth adapter found. Check that Bluetooth is enabled.");
                shared_->running = false;
                return false;
            }
        } catch (const std::exception& error) {
            logError(std::string("BLE initialization failed: ") + error.what());
            shared_->running = false;
            return false;
        }

        scanThread_ = std::thread(&Impl::scanLoop, this);
        return true;
    }

    void stop() {
        if (!shared_->running.exchange(false)) {
            return;
        }

        if (scanThread_.joinable()) {
            scanThread_.join();
        }
        for (std::thread& thread : connectionThreads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        connectionThreads_.clear();

        {
            std::lock_guard<std::mutex> lock(knownMutex_);
            knownAddresses_.clear();
        }

        std::lock_guard<std::mutex> lock(shared_->mutex);
        shared_->devices.clear();
        shared_->activeDevices.clear();
        shared_->globalButtons.clear();
    }

    bool running() const { return shared_->running; }

    static std::vector<ButtonEvent> mergeLocked(Shared& shared) {
        std::set<uint8_t> next;
        for (const auto& entry : shared.devices) {
            const std::set<uint8_t>& buttons = entry.second.buttons();
            next.insert(buttons.begin(), buttons.end());
        }

        std::vector<ButtonEvent> events;
        for (const uint8_t buttonId : shared.globalButtons) {
            if (next.find(buttonId) == next.end()) {
                events.push_back({buttonId, false});
            }
        }
        for (const uint8_t buttonId : next) {
            if (shared.globalButtons.find(buttonId) == shared.globalButtons.end()) {
                events.push_back({buttonId, true});
            }
        }
        shared.globalButtons = std::move(next);
        return events;
    }

    static void handleNotification(const std::shared_ptr<Shared>& shared,
                                   const std::string& address,
                                   const SimpleBLE::ByteArray& payload) {
        if (!shared->running) {
            return;
        }

        std::vector<ButtonEvent> events;
        uint8_t battery = 0;
        bool hasBattery = false;

        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            if (shared->activeDevices.find(address) == shared->activeDevices.end()) {
                return;
            }
            RideDecoder& decoder = shared->devices[address];
            decoder.process(payload.data(), payload.size());

            if (payload.size() >= 3 && payload[0] == kBatteryNotificationType &&
                payload[1] == 0x10 && payload[2] <= 100) {
                battery = payload[2];
                hasBattery = true;
            }

            events = mergeLocked(*shared);
        }

        if (hasBattery && shared->onBattery) {
            shared->onBattery(battery);
        }
        if (!events.empty() && shared->onEvents) {
            shared->onEvents(events);
        }
    }

    static void removeDevice(const std::shared_ptr<Shared>& shared,
                             const std::string& address) {
        if (!shared->running) {
            return;
        }

        std::vector<ButtonEvent> events;
        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            shared->activeDevices.erase(address);
            shared->devices.erase(address);
            events = mergeLocked(*shared);
        }
        if (!events.empty() && shared->onEvents) {
            shared->onEvents(events);
        }
    }

    static std::string readSerial(SimpleBLE::Peripheral& peripheral) {
        try {
            const SimpleBLE::ByteArray data =
                peripheral.read(kDeviceInfoServiceUuid, kSerialCharacteristicUuid);
            std::string serial(reinterpret_cast<const char*>(data.data()), data.size());
            while (!serial.empty() &&
                   (serial.back() == '\0' || serial.back() == '\r' || serial.back() == '\n')) {
                serial.pop_back();
            }
            return serial;
        } catch (const std::exception&) {
            return {};
        }
    }

    static bool isRide(SimpleBLE::Peripheral& peripheral, const RideBridgeOptions& options) {
        const std::string name = peripheral.identifier();
        const std::string address = peripheral.address();

        if (!options.target.empty() && !contains(name, options.target) &&
            !contains(address, options.target) && toLower(options.target) != "zwift ride") {
            return false;
        }

        if (contains(name, "zwift ride")) {
            return true;
        }

        try {
            for (SimpleBLE::Service& service : peripheral.services()) {
                if (toLower(service.uuid()) == toLower(kServiceUuid)) {
                    return true;
                }
            }
        } catch (const std::exception&) {
            // Advertised service discovery is best-effort.
        }

        return false;
    }

    void scanLoop() {
        int scansWithDevices = 0;
        while (shared_->running) {
            bool foundThisScan = false;
            try {
                const std::vector<SimpleBLE::Adapter> adapters =
                    SimpleBLE::Adapter::get_adapters();
                if (adapters.empty()) {
                    logError("No Bluetooth adapter found. Check that Bluetooth is enabled.");
                    waitWhile(shared_->running, shared_->options.reconnectDelayMs);
                    continue;
                }

                SimpleBLE::Adapter adapter = adapters.front();
                logInfo("scanning for " + shared_->options.target + " for " +
                        std::to_string(shared_->options.scanSeconds) + " s...");
                adapter.scan_for(shared_->options.scanSeconds * 1000);

                for (SimpleBLE::Peripheral& peripheral : adapter.scan_get_results()) {
                    if (!shared_->running) {
                        break;
                    }
                    if (!isRide(peripheral, shared_->options)) {
                        continue;
                    }

                    foundThisScan = true;
                    const std::string address = peripheral.address();
                    {
                        std::lock_guard<std::mutex> lock(knownMutex_);
                        if (!knownAddresses_.insert(address).second) {
                            continue;
                        }
                    }

                    logInfo("found controller " + peripheral.identifier() + " [" + address + "]");
                    connectionThreads_.emplace_back(&Impl::connectionLoop, this, peripheral);
                }
            } catch (const std::exception& error) {
                logWarn(std::string("BLE scan failed: ") + error.what());
            }

            // Once controllers have been seen in two consecutive scans, stop
            // scanning while they are connected. The connection threads
            // reconnect on their own if a controller goes away.
            scansWithDevices = foundThisScan ? scansWithDevices + 1 : 0;
            if (scansWithDevices >= 2) {
                break;
            }

            waitWhile(shared_->running, foundThisScan ? 1000
                                                       : shared_->options.reconnectDelayMs);
        }
    }

    void connectionLoop(SimpleBLE::Peripheral peripheral) {
        const std::string address = peripheral.address();
        const std::string name = peripheral.identifier();

        while (shared_->running) {
            try {
                logInfo("connecting to " + name + " [" + address + "]...");
                peripheral.connect();

                uint16_t mtu = 0;
                try {
                    mtu = peripheral.mtu();
                } catch (const std::exception&) {
                    // MTU is informational only.
                }
                logInfo("connected; MTU=" + std::to_string(mtu));

                {
                    std::lock_guard<std::mutex> lock(shared_->mutex);
                    shared_->activeDevices.insert(address);
                }

                const std::string serial = readSerial(peripheral);
                if (!serial.empty()) {
                    logInfo("controller serial: " + serial);
                }

                peripheral.notify(
                    kServiceUuid, kAsyncCharacteristicUuid,
                    [shared = shared_, address](SimpleBLE::ByteArray payload) {
                        handleNotification(shared, address, payload);
                    });

                try {
                    peripheral.indicate(kServiceUuid, kSyncTxCharacteristicUuid,
                                        [](SimpleBLE::ByteArray) {});
                } catch (const std::exception& error) {
                    logWarn(std::string("could not subscribe to handshake indications: ") +
                            error.what());
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                peripheral.write_command(
                    kServiceUuid, kSyncRxCharacteristicUuid,
                    SimpleBLE::ByteArray{kRideOn[0], kRideOn[1], kRideOn[2], kRideOn[3],
                                         kRideOn[4], kRideOn[5]});
                logInfo("RideOn handshake sent");

                while (shared_->running && peripheral.is_connected()) {
                    waitWhile(shared_->running, 500);
                }
            } catch (const std::exception& error) {
                logWarn(std::string("BLE connection failed: ") + error.what());
            }

            removeDevice(shared_, address);

            try {
                if (peripheral.is_connected()) {
                    peripheral.disconnect();
                }
            } catch (const std::exception&) {
                // Already disconnected.
            }

            if (!shared_->running) {
                break;
            }
            logWarn("controller disconnected; retrying in " +
                    std::to_string(shared_->options.reconnectDelayMs) + " ms");
            waitWhile(shared_->running, shared_->options.reconnectDelayMs);
        }
    }

    std::shared_ptr<Shared> shared_;
    std::thread scanThread_;
    std::vector<std::thread> connectionThreads_;
    std::mutex knownMutex_;
    std::set<std::string> knownAddresses_;
};

#else

struct RideBridge::Impl {
    Impl(RideBridgeOptions, EventCallback, BatteryCallback) {}
    bool start() {
        logError("This build has no BLE support (MYBC_WITH_BLE=OFF).");
        return false;
    }
    void stop() {}
    bool running() const { return false; }
};

#endif

RideBridge::RideBridge(RideBridgeOptions options, EventCallback onEvents,
                       BatteryCallback onBattery)
    : impl_(std::make_unique<Impl>(std::move(options), std::move(onEvents),
                                   std::move(onBattery))) {}

RideBridge::~RideBridge() { stop(); }

bool RideBridge::start() { return impl_->start(); }

void RideBridge::stop() { impl_->stop(); }

bool RideBridge::running() const { return impl_->running(); }

}  // namespace mybc::ble

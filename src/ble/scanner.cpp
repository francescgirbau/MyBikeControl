#include "mybc/ble/scanner.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <thread>
#include <utility>

#include "mybc/log.h"
#include "mybc/obc/protocol.h"

#if MYBC_WITH_BLE
#include <simpleble/SimpleBLE.h>
#endif

namespace mybc::ble {
namespace {

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool matches(const std::string& needle, const std::string& haystack) {
    if (needle.empty()) {
        return true;
    }
    return toLower(haystack).find(toLower(needle)) != std::string::npos;
}

}  // namespace

#if MYBC_WITH_BLE

std::vector<DeviceSummary> scan(const ScanOptions& options) {
    std::vector<DeviceSummary> devices;

    try {
        const std::vector<SimpleBLE::Adapter> adapters = SimpleBLE::Adapter::get_adapters();
        if (adapters.empty()) {
            logError("No Bluetooth adapter found. Check that Bluetooth is enabled.");
            return devices;
        }

        SimpleBLE::Adapter adapter = adapters.front();
        logInfo("adapter: " + adapter.identifier() + " (" + adapter.address() + ")");

        adapter.scan_for(options.seconds * 1000);

        for (SimpleBLE::Peripheral& peripheral : adapter.scan_get_results()) {
            DeviceSummary summary;
            summary.name = peripheral.identifier();
            summary.address = peripheral.address();
            summary.rssi = peripheral.rssi();

            if (!options.filter.empty() && !matches(options.filter, summary.name) &&
                !matches(options.filter, summary.address)) {
                continue;
            }

            try {
                summary.connectable = peripheral.is_connectable();
            } catch (const std::exception&) {
                summary.connectable = false;
            }

            try {
                for (SimpleBLE::Service& service : peripheral.services()) {
                    summary.advertisedServices.push_back(service.uuid());
                }
            } catch (const std::exception&) {
                // Advertised service discovery is best-effort on every backend.
            }

            devices.push_back(std::move(summary));
        }
    } catch (const std::exception& error) {
        logError(std::string("BLE scan failed: ") + error.what());
    }

    std::sort(devices.begin(), devices.end(),
              [](const DeviceSummary& a, const DeviceSummary& b) { return a.rssi > b.rssi; });
    return devices;
}

bool inspect(const InspectOptions& options) {
    try {
        const std::vector<SimpleBLE::Adapter> adapters = SimpleBLE::Adapter::get_adapters();
        if (adapters.empty()) {
            logError("No Bluetooth adapter found. Check that Bluetooth is enabled.");
            return false;
        }

        SimpleBLE::Adapter adapter = adapters.front();
        logInfo("scanning for " + std::to_string(options.seconds) + " s to find \"" +
                options.target + "\"...");
        adapter.scan_for(options.seconds * 1000);

        std::vector<SimpleBLE::Peripheral> results = adapter.scan_get_results();
        if (results.empty()) {
            logError("No BLE devices found. Is the controller awake and not connected to Zwift?");
            return false;
        }

        SimpleBLE::Peripheral target;
        bool found = false;

        try {
            size_t consumed = 0;
            const unsigned long index = std::stoul(options.target, &consumed);
            if (consumed == options.target.size() && index < results.size()) {
                target = results[index];
                found = true;
            }
        } catch (const std::exception&) {
            // Not an index; fall through to name/address matching.
        }

        if (!found) {
            for (SimpleBLE::Peripheral& peripheral : results) {
                if (matches(options.target, peripheral.identifier()) ||
                    matches(options.target, peripheral.address())) {
                    target = peripheral;
                    found = true;
                    break;
                }
            }
        }

        if (!found) {
            logError("No device matching \"" + options.target + "\". Available devices:");
            for (size_t i = 0; i < results.size(); ++i) {
                logError("  [" + std::to_string(i) + "] " + results[i].identifier() + " (" +
                         results[i].address() + ")");
            }
            return false;
        }

        logInfo("connecting to " + target.identifier() + " [" + target.address() + "]...");
        target.connect();
        logInfo("connected; MTU=" + std::to_string(target.mtu()));

        for (SimpleBLE::Service& service : target.services()) {
            logInfo("service " + service.uuid());

            for (SimpleBLE::Characteristic& characteristic : service.characteristics()) {
                std::string capabilities;
                for (const std::string& capability : characteristic.capabilities()) {
                    if (!capabilities.empty()) {
                        capabilities += ", ";
                    }
                    capabilities += capability;
                }

                logInfo("  characteristic " + characteristic.uuid() + " [" + capabilities + "]");

                if (options.subscribeNotifications && characteristic.can_notify()) {
                    const std::string label = service.uuid() + "/" + characteristic.uuid();
                    target.notify(service.uuid(), characteristic.uuid(),
                                  [label](SimpleBLE::ByteArray payload) {
                                      logInfo("notify " + label + " <- " +
                                              obc::toHex(payload.data(), payload.size()));
                                  });
                    logInfo("    subscribed to notifications");
                }
            }
        }

        if (options.subscribeNotifications) {
            logInfo("listening for " + std::to_string(options.seconds) +
                    " s; press buttons on the controller now");
            std::this_thread::sleep_for(std::chrono::seconds(options.seconds));
        }

        target.disconnect();
        logInfo("disconnected");
        return true;
    } catch (const std::exception& error) {
        logError(std::string("BLE inspect failed: ") + error.what());
        return false;
    }
}

#else

std::vector<DeviceSummary> scan(const ScanOptions& options) {
    (void)options;
    logError("This build has no BLE support (MYBC_WITH_BLE=OFF).");
    return {};
}

bool inspect(const InspectOptions& options) {
    (void)options;
    logError("This build has no BLE support (MYBC_WITH_BLE=OFF).");
    return false;
}

#endif

}  // namespace mybc::ble

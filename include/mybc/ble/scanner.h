#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mybc::ble {

struct DeviceSummary {
    std::string name;
    std::string address;
    int16_t rssi = 0;
    bool connectable = false;
    std::vector<std::string> advertisedServices;
};

struct ScanOptions {
    int seconds = 8;
    std::string filter;
};

struct InspectOptions {
    std::string target;
    int seconds = 8;
    bool subscribeNotifications = false;
};

/// Scans for `options.seconds` and returns matching devices sorted by RSSI.
std::vector<DeviceSummary> scan(const ScanOptions& options);

/// Connects to a device (by scan index, name or address) and dumps its GATT tree.
bool inspect(const InspectOptions& options);

}  // namespace mybc::ble

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "mybc/ble/ride_decoder.h"

namespace mybc::ble {

struct RideBridgeOptions {
    /// Case-insensitive name/address filter. Defaults to "Zwift Ride".
    std::string target = "Zwift Ride";
    int scanSeconds = 8;
    int reconnectDelayMs = 5000;
};

/// Connects to Zwift Ride controllers over BLE, sends the `RideOn` handshake,
/// and forwards decoded OpenBikeControl button transitions.
///
/// Both Ride halves advertise the same service. The bridge connects to every
/// matching device and merges their states, so a button reported by both sides
/// produces exactly one press/release pair.
class RideBridge {
  public:
    using EventCallback = std::function<void(const std::vector<ButtonEvent>&)>;
    using BatteryCallback = std::function<void(uint8_t)>;

    RideBridge(RideBridgeOptions options, EventCallback onEvents,
               BatteryCallback onBattery = {});
    ~RideBridge();

    RideBridge(const RideBridge&) = delete;
    RideBridge& operator=(const RideBridge&) = delete;

    /// Starts the scan/reconnect thread. Returns false if BLE is unavailable.
    bool start();
    void stop();
    bool running() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mybc::ble

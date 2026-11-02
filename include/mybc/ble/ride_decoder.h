#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <vector>

namespace mybc::ble {

/// One entry from the repeated `analogPaddles` field of `RideKeyPadStatus`.
///
/// Location 0 is the left paddle and location 1 is the right paddle. Locations
/// 2 and 3 are present in some firmware versions but unused by the Ride.
struct RideAnalogPaddle {
    uint8_t location = 0;
    int32_t value = 0;
};

/// Decoded Zwift Ride `RideKeyPadStatus` protobuf message.
///
/// `buttonMap` is active-low: a cleared bit means the physical button is
/// pressed. The masks below match the reference implementation
/// (`zwift_ride.dart`), not the generated protobuf enum.
struct RideKeyPadStatus {
    uint32_t buttonMap = 0;
    std::vector<RideAnalogPaddle> paddles;
};

namespace ride_mask {

constexpr uint32_t kLeft = 0x00001;
constexpr uint32_t kUp = 0x00002;
constexpr uint32_t kRight = 0x00004;
constexpr uint32_t kDown = 0x00008;

constexpr uint32_t kA = 0x00010;
constexpr uint32_t kB = 0x00020;
constexpr uint32_t kY = 0x00040;
constexpr uint32_t kZ = 0x00080;

constexpr uint32_t kShiftUpLeft = 0x00100;
constexpr uint32_t kShiftDownLeft = 0x00200;
constexpr uint32_t kPowerUpLeft = 0x00400;
constexpr uint32_t kOnOffLeft = 0x00800;

constexpr uint32_t kShiftUpRight = 0x01000;
constexpr uint32_t kShiftDownRight = 0x02000;
constexpr uint32_t kPowerUpRight = 0x04000;
constexpr uint32_t kOnOffRight = 0x08000;

}  // namespace ride_mask

/// Parses the protobuf body (without the leading 0x23 notification type).
std::optional<RideKeyPadStatus> parseRideKeyPadStatus(const uint8_t* data, size_t size);

/// Parses a complete `00000002-...` notification payload.
///
/// Returns nullopt for keep-alives and for any message whose first byte is not
/// `0x23` (RideKeyPadStatus).
std::optional<RideKeyPadStatus> parseRideNotification(const uint8_t* data, size_t size);

/// A transition of a standard OpenBikeControl button.
struct ButtonEvent {
    uint8_t buttonId = 0;
    bool pressed = false;
};

/// Stateful decoder that turns Ride notifications into OpenBikeControl button
/// transitions.
///
/// The controller repeats the complete state many times per second while a
/// button is held. This class keeps the previous state and only emits events
/// when a standard OBC button actually changes.
class RideDecoder {
  public:
    std::vector<ButtonEvent> process(const uint8_t* data, size_t size);
    std::vector<ButtonEvent> processStatus(const RideKeyPadStatus& status);

    const std::set<uint8_t>& buttons() const { return buttons_; }
    void reset();

  private:
    std::set<uint8_t> buttons_;
    bool leftPaddle_ = false;
    bool rightPaddle_ = false;
};

}  // namespace mybc::ble

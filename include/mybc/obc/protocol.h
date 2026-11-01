#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace mybc::obc {

// OpenBikeControl message types (PROTOCOL.md / MDNS.md).
constexpr uint8_t kMsgButtonState = 0x01;
constexpr uint8_t kMsgDeviceStatus = 0x02;
constexpr uint8_t kMsgHapticFeedback = 0x03;
constexpr uint8_t kMsgAppInfo = 0x04;

// Standard button IDs (subset used by this bridge).
constexpr uint8_t kButtonShiftUp = 0x01;
constexpr uint8_t kButtonShiftDown = 0x02;
constexpr uint8_t kButtonGearSet = 0x03;
constexpr uint8_t kButtonUp = 0x10;
constexpr uint8_t kButtonDown = 0x11;
constexpr uint8_t kButtonLeft = 0x12;
constexpr uint8_t kButtonRight = 0x13;
constexpr uint8_t kButtonSelect = 0x14;
constexpr uint8_t kButtonBack = 0x15;
constexpr uint8_t kButtonMenu = 0x16;
constexpr uint8_t kButtonHome = 0x17;
constexpr uint8_t kButtonSteerLeft = 0x18;
constexpr uint8_t kButtonSteerRight = 0x19;
constexpr uint8_t kButtonBrake = 0x1A;
constexpr uint8_t kButtonSteeringAngle = 0x1B;
constexpr uint8_t kButtonEmote = 0x20;
constexpr uint8_t kButtonIncreaseDifficulty = 0x30;
constexpr uint8_t kButtonDecreaseDifficulty = 0x31;
constexpr uint8_t kButtonPause = 0x33;
constexpr uint8_t kButtonResume = 0x34;
constexpr uint8_t kButtonCameraView = 0x40;
constexpr uint8_t kButtonHudToggle = 0x44;

constexpr uint8_t kStateReleased = 0x00;
constexpr uint8_t kStatePressed = 0x01;

using Bytes = std::vector<uint8_t>;

std::string buttonName(uint8_t buttonId);

/// Builds a button-state message: [0x01, id, state, id, state, ...].
/// `pairs` must contain an even number of bytes.
Bytes encodeButtonState(const Bytes& pairs);
Bytes encodeButtonState(std::initializer_list<uint8_t> pairs);

/// Builds a device-status message: [0x02, battery, connected].
Bytes encodeDeviceStatus(uint8_t batteryPercent, bool connected);

struct HapticCommand {
    uint8_t pattern = 0;
    uint8_t duration = 0;
    uint8_t intensity = 0;
};

struct AppInfo {
    uint8_t version = 1;
    std::string appId;
    std::string appVersion;
    std::vector<uint8_t> supportedButtons;

    bool supportsAllButtons() const { return supportedButtons.empty(); }
    bool supports(uint8_t buttonId) const;
};

enum class IncomingKind { AppInfo, HapticFeedback, Unknown };

struct IncomingMessage {
    IncomingKind kind = IncomingKind::Unknown;
    uint8_t rawType = 0;
    AppInfo appInfo;
    HapticCommand haptic;
};

/// Incremental parser for the messages an app sends to a device.
///
/// The TCP transport has no length prefix for button-state messages, so this
/// parser only handles the fixed-size haptic message and the self-describing
/// app-info message. Unknown bytes are reported and skipped one at a time.
class StreamParser {
  public:
    std::vector<IncomingMessage> feed(const uint8_t* data, size_t length);
    void reset();

  private:
    std::vector<uint8_t> buffer_;
};

std::string toHex(const uint8_t* data, size_t length);
std::string toHex(const Bytes& bytes);

}  // namespace mybc::obc

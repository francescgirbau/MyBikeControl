#include "mybc/obc/protocol.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <utility>

namespace mybc::obc {
namespace {

constexpr size_t kMaxBuffer = 4096;
constexpr size_t kMaxStringField = 64;

}  // namespace

std::string buttonName(uint8_t buttonId) {
    switch (buttonId) {
        case kButtonShiftUp: return "Shift Up";
        case kButtonShiftDown: return "Shift Down";
        case kButtonGearSet: return "Gear Set";
        case kButtonUp: return "Up";
        case kButtonDown: return "Down";
        case kButtonLeft: return "Left";
        case kButtonRight: return "Right";
        case kButtonSelect: return "Select";
        case kButtonBack: return "Back";
        case kButtonMenu: return "Menu";
        case kButtonHome: return "Home";
        case kButtonSteerLeft: return "Steer Left";
        case kButtonSteerRight: return "Steer Right";
        case kButtonBrake: return "Brake";
        case kButtonSteeringAngle: return "Steering Angle";
        case kButtonEmote: return "Emote";
        case kButtonIncreaseDifficulty: return "Increase Difficulty";
        case kButtonDecreaseDifficulty: return "Decrease Difficulty";
        case kButtonPause: return "Pause";
        case kButtonResume: return "Resume";
        case kButtonCameraView: return "Camera View";
        case kButtonHudToggle: return "HUD Toggle";
        default: return "Unknown";
    }
}

Bytes encodeButtonState(const Bytes& pairs) {
    if (pairs.size() % 2 != 0) {
        return {};
    }

    Bytes message;
    message.reserve(pairs.size() + 1);
    message.push_back(kMsgButtonState);
    message.insert(message.end(), pairs.begin(), pairs.end());
    return message;
}

Bytes encodeButtonState(std::initializer_list<uint8_t> pairs) {
    return encodeButtonState(Bytes(pairs));
}

Bytes encodeDeviceStatus(uint8_t batteryPercent, bool connected) {
    return {kMsgDeviceStatus, batteryPercent, static_cast<uint8_t>(connected ? 1 : 0)};
}

bool AppInfo::supports(uint8_t buttonId) const {
    if (supportedButtons.empty()) {
        return true;
    }
    return std::find(supportedButtons.begin(), supportedButtons.end(), buttonId) !=
           supportedButtons.end();
}

std::vector<IncomingMessage> StreamParser::feed(const uint8_t* data, size_t length) {
    std::vector<IncomingMessage> messages;

    if (data != nullptr && length > 0) {
        buffer_.insert(buffer_.end(), data, data + length);
    }

    if (buffer_.size() > kMaxBuffer) {
        buffer_.clear();
        IncomingMessage overflow;
        overflow.kind = IncomingKind::Unknown;
        messages.push_back(overflow);
        return messages;
    }

    size_t offset = 0;
    while (offset < buffer_.size()) {
        const uint8_t type = buffer_[offset];
        const size_t available = buffer_.size() - offset;

        if (type == kMsgHapticFeedback) {
            if (available < 4) {
                break;
            }
            IncomingMessage message;
            message.kind = IncomingKind::HapticFeedback;
            message.rawType = type;
            message.haptic.pattern = buffer_[offset + 1];
            message.haptic.duration = buffer_[offset + 2];
            message.haptic.intensity = buffer_[offset + 3];
            messages.push_back(message);
            offset += 4;
            continue;
        }

        if (type == kMsgAppInfo) {
            if (available < 3) {
                break;
            }

            const uint8_t appIdLength = buffer_[offset + 2];
            if (appIdLength > kMaxStringField) {
                IncomingMessage unknown;
                unknown.kind = IncomingKind::Unknown;
                unknown.rawType = type;
                messages.push_back(unknown);
                offset += 1;
                continue;
            }

            const size_t appIdEnd = offset + 3 + appIdLength;
            if (buffer_.size() < appIdEnd + 1) {
                break;
            }

            const uint8_t appVersionLength = buffer_[appIdEnd];
            if (appVersionLength > kMaxStringField) {
                IncomingMessage unknown;
                unknown.kind = IncomingKind::Unknown;
                unknown.rawType = type;
                messages.push_back(unknown);
                offset += 1;
                continue;
            }

            const size_t versionEnd = appIdEnd + 1 + appVersionLength;
            if (buffer_.size() < versionEnd + 1) {
                break;
            }

            const uint8_t buttonCount = buffer_[versionEnd];
            const size_t messageEnd = versionEnd + 1 + buttonCount;
            if (buffer_.size() < messageEnd) {
                break;
            }

            IncomingMessage message;
            message.kind = IncomingKind::AppInfo;
            message.rawType = type;
            message.appInfo.version = buffer_[offset + 1];
            message.appInfo.appId.assign(
                reinterpret_cast<const char*>(buffer_.data() + offset + 3), appIdLength);
            message.appInfo.appVersion.assign(
                reinterpret_cast<const char*>(buffer_.data() + appIdEnd + 1), appVersionLength);
            message.appInfo.supportedButtons.assign(
                buffer_.begin() + static_cast<std::ptrdiff_t>(versionEnd + 1),
                buffer_.begin() + static_cast<std::ptrdiff_t>(messageEnd));
            messages.push_back(std::move(message));
            offset = messageEnd;
            continue;
        }

        IncomingMessage unknown;
        unknown.kind = IncomingKind::Unknown;
        unknown.rawType = type;
        messages.push_back(unknown);
        offset += 1;
    }

    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(offset));
    return messages;
}

void StreamParser::reset() { buffer_.clear(); }

std::string toHex(const uint8_t* data, size_t length) {
    if (data == nullptr || length == 0) {
        return {};
    }

    static const char* digits = "0123456789ABCDEF";
    std::string out;
    out.reserve(length * 3);
    for (size_t i = 0; i < length; ++i) {
        if (i != 0) {
            out.push_back(' ');
        }
        out.push_back(digits[(data[i] >> 4) & 0x0F]);
        out.push_back(digits[data[i] & 0x0F]);
    }
    return out;
}

std::string toHex(const Bytes& bytes) { return toHex(bytes.data(), bytes.size()); }

}  // namespace mybc::obc

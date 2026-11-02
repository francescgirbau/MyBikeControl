#include "mybc/ble/ride_decoder.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "mybc/obc/protocol.h"

namespace mybc::ble {
namespace {

constexpr uint8_t kRideKeyPadStatusType = 0x23;
constexpr int32_t kPaddlePressThreshold = 25;
constexpr int32_t kPaddleReleaseThreshold = 20;

bool readVarint(const uint8_t*& cursor, const uint8_t* end, uint64_t& value) {
    value = 0;
    int shift = 0;
    while (cursor < end && shift <= 63) {
        const uint8_t byte = *cursor++;
        value |= static_cast<uint64_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            return true;
        }
        shift += 7;
    }
    return false;
}

bool skipField(const uint8_t*& cursor, const uint8_t* end, uint8_t wireType) {
    switch (wireType) {
        case 0: {
            uint64_t ignored = 0;
            return readVarint(cursor, end, ignored);
        }
        case 1:
            if (end - cursor < 8) {
                return false;
            }
            cursor += 8;
            return true;
        case 2: {
            uint64_t length = 0;
            if (!readVarint(cursor, end, length) ||
                length > static_cast<uint64_t>(end - cursor)) {
                return false;
            }
            cursor += static_cast<std::ptrdiff_t>(length);
            return true;
        }
        case 5:
            if (end - cursor < 4) {
                return false;
            }
            cursor += 4;
            return true;
        default:
            return false;
    }
}

int32_t signedInt32(uint64_t value) {
    if (value <= static_cast<uint64_t>(INT32_MAX)) {
        return static_cast<int32_t>(value);
    }
    return static_cast<int32_t>(static_cast<int64_t>(value) - 0x100000000LL);
}

bool parsePaddle(const uint8_t* data, size_t size, RideAnalogPaddle& paddle) {
    const uint8_t* cursor = data;
    const uint8_t* end = data + size;
    bool hasLocation = false;
    bool hasValue = false;

    while (cursor < end) {
        uint64_t key = 0;
        if (!readVarint(cursor, end, key)) {
            return false;
        }

        const uint32_t field = static_cast<uint32_t>(key >> 3);
        const uint8_t wireType = static_cast<uint8_t>(key & 0x07);
        if (field == 0) {
            return false;
        }

        if (field == 1 && wireType == 0) {
            uint64_t value = 0;
            if (!readVarint(cursor, end, value)) {
                return false;
            }
            paddle.location = static_cast<uint8_t>(value);
            hasLocation = true;
        } else if (field == 2 && wireType == 0) {
            uint64_t value = 0;
            if (!readVarint(cursor, end, value)) {
                return false;
            }
            paddle.value = signedInt32(value);
            hasValue = true;
        } else if (!skipField(cursor, end, wireType)) {
            return false;
        }
    }

    return hasLocation && hasValue;
}

void updatePaddle(bool& pressed, int32_t value) {
    const int64_t magnitude =
        value < 0 ? -static_cast<int64_t>(value) : static_cast<int64_t>(value);
    if (magnitude >= kPaddlePressThreshold) {
        pressed = true;
    } else if (magnitude <= kPaddleReleaseThreshold) {
        pressed = false;
    }
}

std::set<uint8_t> mapStatusToButtons(const RideKeyPadStatus& status, bool& leftPaddle,
                                     bool& rightPaddle) {
    std::set<uint8_t> buttons;
    const auto isPressed = [&status](uint32_t mask) {
        // The firmware sends an active-low mask: 0 = pressed.
        return (status.buttonMap & mask) == 0;
    };

    if (isPressed(ride_mask::kLeft)) {
        buttons.insert(obc::kButtonSteerLeft);
    }
    if (isPressed(ride_mask::kRight)) {
        buttons.insert(obc::kButtonSteerRight);
    }
    if (isPressed(ride_mask::kUp)) {
        buttons.insert(obc::kButtonUp);
    }
    if (isPressed(ride_mask::kDown)) {
        buttons.insert(obc::kButtonDown);
    }
    if (isPressed(ride_mask::kA)) {
        buttons.insert(obc::kButtonSelect);
    }
    if (isPressed(ride_mask::kB)) {
        buttons.insert(obc::kButtonBack);
    }
    if (isPressed(ride_mask::kY)) {
        buttons.insert(obc::kButtonMenu);
    }
    if (isPressed(ride_mask::kZ)) {
        buttons.insert(obc::kButtonEmote);
    }

    // The Ride's left-side shift controls all mean "shift down"; the right-side
    // ones all mean "shift up" (see ZwiftButtons in the reference app).
    if (isPressed(ride_mask::kShiftUpLeft) || isPressed(ride_mask::kShiftDownLeft) ||
        isPressed(ride_mask::kPowerUpLeft)) {
        buttons.insert(obc::kButtonShiftDown);
    }
    if (isPressed(ride_mask::kShiftUpRight) || isPressed(ride_mask::kShiftDownRight) ||
        isPressed(ride_mask::kPowerUpRight)) {
        buttons.insert(obc::kButtonShiftUp);
    }

    if (isPressed(ride_mask::kOnOffLeft) || isPressed(ride_mask::kOnOffRight)) {
        buttons.insert(obc::kButtonHudToggle);
    }

    // Analog paddles: location 0 is the left paddle (shift down), location 1 is
    // the right paddle (shift up). The controller sends all locations on every
    // notification, so a missing location means "released".
    int32_t leftValue = 0;
    int32_t rightValue = 0;
    for (const RideAnalogPaddle& paddle : status.paddles) {
        if (paddle.location == 0) {
            leftValue = paddle.value;
        } else if (paddle.location == 1) {
            rightValue = paddle.value;
        }
    }

    updatePaddle(leftPaddle, leftValue);
    updatePaddle(rightPaddle, rightValue);
    if (leftPaddle) {
        buttons.insert(obc::kButtonShiftDown);
    }
    if (rightPaddle) {
        buttons.insert(obc::kButtonShiftUp);
    }

    return buttons;
}

}  // namespace

std::optional<RideKeyPadStatus> parseRideKeyPadStatus(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return std::nullopt;
    }

    RideKeyPadStatus status;
    bool hasButtonMap = false;
    const uint8_t* cursor = data;
    const uint8_t* end = data + size;

    while (cursor < end) {
        uint64_t key = 0;
        if (!readVarint(cursor, end, key)) {
            return std::nullopt;
        }

        const uint32_t field = static_cast<uint32_t>(key >> 3);
        const uint8_t wireType = static_cast<uint8_t>(key & 0x07);
        if (field == 0) {
            return std::nullopt;
        }

        if (field == 1 && wireType == 0) {
            uint64_t value = 0;
            if (!readVarint(cursor, end, value)) {
                return std::nullopt;
            }
            status.buttonMap = static_cast<uint32_t>(value);
            hasButtonMap = true;
        } else if (field == 3 && wireType == 2) {
            uint64_t length = 0;
            if (!readVarint(cursor, end, length) ||
                length > static_cast<uint64_t>(end - cursor)) {
                return std::nullopt;
            }

            const uint8_t* paddleEnd = cursor + static_cast<std::ptrdiff_t>(length);
            RideAnalogPaddle paddle;
            if (!parsePaddle(cursor, static_cast<size_t>(length), paddle)) {
                return std::nullopt;
            }
            status.paddles.push_back(paddle);
            cursor = paddleEnd;
        } else if (!skipField(cursor, end, wireType)) {
            return std::nullopt;
        }
    }

    if (!hasButtonMap) {
        return std::nullopt;
    }
    return status;
}

std::optional<RideKeyPadStatus> parseRideNotification(const uint8_t* data, size_t size) {
    if (data == nullptr || size < 2 || data[0] != kRideKeyPadStatusType) {
        return std::nullopt;
    }
    return parseRideKeyPadStatus(data + 1, size - 1);
}

std::vector<ButtonEvent> RideDecoder::process(const uint8_t* data, size_t size) {
    const std::optional<RideKeyPadStatus> status = parseRideNotification(data, size);
    if (!status) {
        return {};
    }
    return processStatus(*status);
}

std::vector<ButtonEvent> RideDecoder::processStatus(const RideKeyPadStatus& status) {
    std::set<uint8_t> next = mapStatusToButtons(status, leftPaddle_, rightPaddle_);
    std::vector<ButtonEvent> events;

    for (const uint8_t buttonId : buttons_) {
        if (next.find(buttonId) == next.end()) {
            events.push_back({buttonId, false});
        }
    }
    for (const uint8_t buttonId : next) {
        if (buttons_.find(buttonId) == buttons_.end()) {
            events.push_back({buttonId, true});
        }
    }

    buttons_ = std::move(next);
    return events;
}

void RideDecoder::reset() {
    buttons_.clear();
    leftPaddle_ = false;
    rightPaddle_ = false;
}

}  // namespace mybc::ble

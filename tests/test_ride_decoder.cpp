#include "mybc/ble/ride_decoder.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "mybc/obc/protocol.h"

using namespace mybc::ble;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        ++g_failures;
    }
}

std::vector<uint8_t> hex(const std::string& text) {
    std::vector<uint8_t> bytes;
    int high = -1;
    for (const char c : text) {
        int value = -1;
        if (c >= '0' && c <= '9') {
            value = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            value = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            value = c - 'A' + 10;
        } else {
            continue;
        }
        if (high < 0) {
            high = value;
        } else {
            bytes.push_back(static_cast<uint8_t>((high << 4) | value));
            high = -1;
        }
    }
    return bytes;
}

std::vector<ButtonEvent> feed(RideDecoder& decoder, const std::string& text) {
    const std::vector<uint8_t> bytes = hex(text);
    return decoder.process(bytes.data(), bytes.size());
}

void expectSingle(const std::vector<ButtonEvent>& events, uint8_t buttonId, bool pressed,
                  const std::string& what) {
    check(events.size() == 1, what + ": one event");
    if (events.size() == 1) {
        check(events[0].buttonId == buttonId, what + ": button id");
        check(events[0].pressed == pressed, what + ": state");
    }
}

const char* kReleased = "2308ffffffff0f1a04080010001a0408011000";
const char* kShiftUpRight = "2308ffdfffff0f1a04080010001a0408011000";
const char* kShiftUpLeftAndPaddle =
    "2308fffdffff0f1a05080010c7011a04080110001a04080210001a0408031000";
const char* kAnalogRight =
    "2308ffffffff0f1a04080010001a05080110c801";
const char* kAnalogRightBelowThreshold =
    "2308ffffffff0f1a04080010001a040801100a";
const char* kNavigationUp =
    "2308fdffffff0f1a04080010001a04080110001a04080210001a0408031000";
const char* kNavigationDown =
    "2308f7ffffff0f1a04080010001a04080110001a04080210001a0408031000";
const char* kNavigationLeft =
    "2308feffffff0f1a04080010001a04080110001a04080210001a0408031000";
const char* kNavigationRight =
    "2308fbffffff0f1a04080010001a04080110001a04080210001a0408031000";
const char* kY = "2308bfffffff0f1a04080010001a0408011000";
const char* kB = "2308dfffffff0f1a04080010001a0408011000";
const char* kZ = "2308fffeffff0f1a04080010001a0408011000";
const char* kA = "2308efffffff0f1a04080010001a0408011000";
const char* kOnOffRight = "2308fffffdff0f1a04080010001a0408011000";

void testParseReleased() {
    const std::vector<uint8_t> bytes = hex(kReleased);
    const std::optional<RideKeyPadStatus> status =
        parseRideNotification(bytes.data(), bytes.size());
    check(status.has_value(), "parse: released has value");
    if (status) {
        check(status->buttonMap == 0xFFFFFFFFu, "parse: released button map");
        check(status->paddles.size() == 2, "parse: released paddles");
        check(status->paddles[0].location == 0 && status->paddles[0].value == 0,
              "parse: left paddle released");
        check(status->paddles[1].location == 1 && status->paddles[1].value == 0,
              "parse: right paddle released");
    }

    RideDecoder decoder;
    check(feed(decoder, kReleased).empty(), "released: no events");
    check(decoder.buttons().empty(), "released: no buttons held");
}

void testShiftUpRight() {
    RideDecoder decoder;
    expectSingle(feed(decoder, kShiftUpRight), mybc::obc::kButtonShiftUp, true,
                 "shift up right press");
    check(feed(decoder, kShiftUpRight).empty(), "shift up right: repeated state ignored");
    expectSingle(feed(decoder, kReleased), mybc::obc::kButtonShiftUp, false,
                 "shift up right release");
    check(feed(decoder, kReleased).empty(), "shift up right: repeated release ignored");
}

void testShiftDownLeftAndAnalog() {
    RideDecoder decoder;
    expectSingle(feed(decoder, kShiftUpLeftAndPaddle), mybc::obc::kButtonShiftDown, true,
                 "shift down left press");
    expectSingle(feed(decoder, kReleased), mybc::obc::kButtonShiftDown, false,
                 "shift down left release");
}

void testAnalogRight() {
    RideDecoder decoder;
    expectSingle(feed(decoder, kAnalogRight), mybc::obc::kButtonShiftUp, true,
                 "analog right press");
    expectSingle(feed(decoder, kAnalogRightBelowThreshold), mybc::obc::kButtonShiftUp, false,
                 "analog right release");
}

void testNavigationAndFaceButtons() {
    struct Case {
        const char* payload;
        uint8_t buttonId;
        const char* name;
    };

    const Case cases[] = {
        {kNavigationUp, mybc::obc::kButtonUp, "navigation up"},
        {kNavigationDown, mybc::obc::kButtonDown, "navigation down"},
        {kNavigationLeft, mybc::obc::kButtonSteerLeft, "navigation left"},
        {kNavigationRight, mybc::obc::kButtonSteerRight, "navigation right"},
        {kY, mybc::obc::kButtonMenu, "Y"},
        {kB, mybc::obc::kButtonBack, "B"},
        {kZ, mybc::obc::kButtonEmote, "Z"},
        {kA, mybc::obc::kButtonSelect, "A"},
        {kOnOffRight, mybc::obc::kButtonHudToggle, "on/off right"},
    };

    for (const Case& item : cases) {
        RideDecoder decoder;
        expectSingle(feed(decoder, item.payload), item.buttonId, true,
                     std::string(item.name) + " press");
        expectSingle(feed(decoder, kReleased), item.buttonId, false,
                     std::string(item.name) + " release");
    }
}

void testIgnoresOtherNotificationTypes() {
    RideDecoder decoder;
    check(feed(decoder, "15").empty(), "keepalive ignored");
    check(feed(decoder, "2a08031210120e081810001890032205ffffffff1f").empty(),
          "vendor message ignored");
    check(decoder.buttons().empty(), "other types leave state empty");
}

void testMalformedMessagesAreRejected() {
    RideDecoder decoder;
    check(feed(decoder, "").empty(), "empty payload ignored");
    check(feed(decoder, "23").empty(), "empty protobuf ignored");
    check(feed(decoder, "2308").empty(), "truncated varint ignored");
    check(feed(decoder, "2308ffffffff0f1a02").empty(), "truncated paddle ignored");
    check(decoder.buttons().empty(), "malformed payload leaves state empty");
}

void testReset() {
    RideDecoder decoder;
    expectSingle(feed(decoder, kShiftUpRight), mybc::obc::kButtonShiftUp, true,
                 "reset: press");
    decoder.reset();
    check(decoder.buttons().empty(), "reset: state cleared");
    expectSingle(feed(decoder, kShiftUpRight), mybc::obc::kButtonShiftUp, true,
                 "reset: press after reset");
}

}  // namespace

int main() {
    testParseReleased();
    testShiftUpRight();
    testShiftDownLeftAndAnalog();
    testAnalogRight();
    testNavigationAndFaceButtons();
    testIgnoresOtherNotificationTypes();
    testMalformedMessagesAreRejected();
    testReset();

    if (g_failures != 0) {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "all ride decoder tests passed\n";
    return 0;
}

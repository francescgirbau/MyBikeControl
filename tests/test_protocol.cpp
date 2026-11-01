#include "mybc/obc/protocol.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using namespace mybc::obc;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        ++g_failures;
    }
}

void testButtonStateEncoding() {
    const Bytes message = encodeButtonState({kButtonShiftUp, kStatePressed});
    check(message.size() == 3, "button state: size");
    check(message[0] == kMsgButtonState, "button state: type");
    check(message[1] == kButtonShiftUp, "button state: id");
    check(message[2] == kStatePressed, "button state: state");

    const Bytes twoButtons =
        encodeButtonState({kButtonShiftUp, kStatePressed, kButtonShiftDown, kStateReleased});
    check(twoButtons.size() == 5, "two buttons: size");
    check(twoButtons[3] == kButtonShiftDown, "two buttons: second id");
    check(twoButtons[4] == kStateReleased, "two buttons: second state");

    check(encodeButtonState({kButtonShiftUp}).empty(), "odd pairs rejected");
}

void testDeviceStatusEncoding() {
    const Bytes message = encodeDeviceStatus(85, true);
    check(message.size() == 3, "status: size");
    check(message[0] == kMsgDeviceStatus, "status: type");
    check(message[1] == 85, "status: battery");
    check(message[2] == 1, "status: connected");

    const Bytes unknownBattery = encodeDeviceStatus(0xFF, false);
    check(unknownBattery[1] == 0xFF, "status: unknown battery");
    check(unknownBattery[2] == 0, "status: disconnected");
}

std::vector<uint8_t> exampleAppInfo() {
    // From MDNS.md: app "zwift", version "1.52.0", buttons 01 02 10 14.
    return {0x04, 0x01, 0x05, 'z',  'w',  'i',  'f',  't',  0x06, '1',
            '.',  '5',  '2',  '.',  '0',  0x04, 0x01, 0x02, 0x10, 0x14};
}

void testAppInfoParsing() {
    const std::vector<uint8_t> message = exampleAppInfo();

    StreamParser parser;
    check(parser.feed(message.data(), 4).empty(), "app info: waits for full message");

    const std::vector<IncomingMessage> parsed =
        parser.feed(message.data() + 4, message.size() - 4);
    check(parsed.size() == 1, "app info: one message");
    check(parsed[0].kind == IncomingKind::AppInfo, "app info: kind");
    check(parsed[0].appInfo.version == 1, "app info: version");
    check(parsed[0].appInfo.appId == "zwift", "app info: app id");
    check(parsed[0].appInfo.appVersion == "1.52.0", "app info: app version");
    check(parsed[0].appInfo.supportedButtons ==
              std::vector<uint8_t>({0x01, 0x02, 0x10, 0x14}),
          "app info: buttons");
    check(parsed[0].appInfo.supports(kButtonShiftUp), "app info: supports shift up");
    check(!parsed[0].appInfo.supports(kButtonSteerLeft), "app info: lacks steer left");
}

void testHapticParsing() {
    const std::vector<uint8_t> message = {0x03, 0x02, 0x14, 0x80};

    StreamParser parser;
    const std::vector<IncomingMessage> parsed = parser.feed(message.data(), message.size());
    check(parsed.size() == 1, "haptic: one message");
    check(parsed[0].kind == IncomingKind::HapticFeedback, "haptic: kind");
    check(parsed[0].haptic.pattern == 0x02, "haptic: pattern");
    check(parsed[0].haptic.duration == 0x14, "haptic: duration");
    check(parsed[0].haptic.intensity == 0x80, "haptic: intensity");
}

void testMultipleMessagesInOneChunk() {
    std::vector<uint8_t> stream = exampleAppInfo();
    stream.insert(stream.end(), {0x03, 0x01, 0x00, 0x00});

    StreamParser parser;
    const std::vector<IncomingMessage> parsed = parser.feed(stream.data(), stream.size());
    check(parsed.size() == 2, "stream: two messages");
    check(parsed[0].kind == IncomingKind::AppInfo, "stream: first is app info");
    check(parsed[1].kind == IncomingKind::HapticFeedback, "stream: second is haptic");
}

void testUnknownByteIsSkipped() {
    const std::vector<uint8_t> garbage = {0x7F};
    const std::vector<uint8_t> haptic = {0x03, 0x00, 0x00, 0x00};

    StreamParser parser;
    const std::vector<IncomingMessage> first = parser.feed(garbage.data(), garbage.size());
    check(first.size() == 1 && first[0].kind == IncomingKind::Unknown, "garbage reported");

    const std::vector<IncomingMessage> second = parser.feed(haptic.data(), haptic.size());
    check(second.size() == 1 && second[0].kind == IncomingKind::HapticFeedback,
          "parser recovers after garbage");
}

void testOversizedLengthIsRejected() {
    const std::vector<uint8_t> bogus = {0x04, 0x01, 0xFF};
    StreamParser parser;
    const std::vector<IncomingMessage> parsed = parser.feed(bogus.data(), bogus.size());
    check(!parsed.empty() && parsed[0].kind == IncomingKind::Unknown,
          "oversized app id rejected");

    const std::vector<uint8_t> haptic = {0x03, 0x01, 0x00, 0x00};
    const std::vector<IncomingMessage> recovered = parser.feed(haptic.data(), haptic.size());
    check(recovered.size() == 1 && recovered[0].kind == IncomingKind::HapticFeedback,
          "parser recovers after oversized length");
}

void testHexDump() {
    const Bytes bytes = {0x01, 0xAB, 0x00};
    check(toHex(bytes) == "01 AB 00", "hex dump");
    check(toHex(nullptr, 0).empty(), "hex dump: empty");
}

void testButtonNames() {
    check(buttonName(kButtonShiftUp) == "Shift Up", "button name: shift up");
    check(buttonName(kButtonSteerLeft) == "Steer Left", "button name: steer left");
    check(buttonName(0xEE) == "Unknown", "button name: unknown");
}

}  // namespace

int main() {
    testButtonStateEncoding();
    testDeviceStatusEncoding();
    testAppInfoParsing();
    testHapticParsing();
    testMultipleMessagesInOneChunk();
    testUnknownByteIsSkipped();
    testOversizedLengthIsRejected();
    testHexDump();
    testButtonNames();

    if (g_failures != 0) {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "all tests passed\n";
    return 0;
}

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "mybc/log.h"
#include "mybc/obc/mdns.h"
#include "mybc/obc/server.h"

namespace obc = mybc::obc;

namespace {

constexpr const char* kVersion = "0.1.0";

volatile std::sig_atomic_t g_stop = 0;

void handleSignal(int) { g_stop = 1; }

void printUsage() {
    std::cout << "MyBikeControl " << kVersion
              << " - Zwift Ride/Play/Click to OpenBikeControl bridge\n\n"
              << "Usage:\n"
              << "  mybc serve   [--port N] [--name TEXT] [--id TEXT] [--no-mdns] [--demo]\n"
              << "  mybc version\n"
              << "  mybc help\n\n"
              << "Commands:\n"
              << "  serve    Advertise an OpenBikeControl device over mDNS and TCP.\n"
              << "           --demo sends synthetic shift up/down without hardware.\n\n"
              << "Options:\n"
              << "  --port N      TCP port for serve (default 8080, 0 = random).\n"
              << "  --name S      Device name advertised to the trainer app.\n"
              << "  --id S        Unique device id advertised in the mDNS TXT record.\n"
              << "  --no-mdns     Skip mDNS advertising (manual TCP testing).\n"
              << "  --demo        Serve without a controller and cycle gears.\n"
              << "  --verbose     Enable debug logs.\n";
}

std::optional<std::string> optionValue(const std::vector<std::string>& args,
                                       const std::string& name) {
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == name) {
            return args[i + 1];
        }
    }
    return std::nullopt;
}

bool hasOption(const std::vector<std::string>& args, const std::string& name) {
    return std::find(args.begin(), args.end(), name) != args.end();
}

int runServe(const std::vector<std::string>& args) {
    uint16_t port = 8080;
    if (const auto value = optionValue(args, "--port")) {
        const int parsed = std::atoi(value->c_str());
        if (parsed < 0 || parsed > 65535) {
            mybc::logError("invalid --port value: " + *value);
            return 2;
        }
        port = static_cast<uint16_t>(parsed);
    }

    obc::DeviceInfo info;
    if (const auto value = optionValue(args, "--name")) {
        info.name = *value;
    }
    if (const auto value = optionValue(args, "--id")) {
        info.id = *value;
    }

    const bool noMdns = hasOption(args, "--no-mdns");
    const bool demo = hasOption(args, "--demo");

    obc::Server server(info, port);

    server.setOnClientConnected([] {
        mybc::logInfo("trainer app connected; forwarding shifts");
    });

    server.setOnAppInfo([](const obc::AppInfo& app) {
        std::string buttons;
        for (const uint8_t buttonId : app.supportedButtons) {
            if (!buttons.empty()) {
                buttons += ", ";
            }
            buttons += obc::buttonName(buttonId);
        }
        mybc::logInfo("app capabilities: " +
                      (app.supportsAllButtons() ? std::string("all buttons") : buttons));
    });

    server.setOnHaptic([](const obc::HapticCommand& haptic) {
        mybc::logInfo("haptic request: pattern=" + std::to_string(haptic.pattern) +
                      " duration=" + std::to_string(haptic.duration) +
                      " intensity=" + std::to_string(haptic.intensity));
    });

    if (!server.start()) {
        return 1;
    }

    obc::MdnsAdvertiser mdns(info.name, server.port(), info);
    if (!noMdns && !mdns.start()) {
        mybc::logWarn("continuing without mDNS; the trainer app will not discover the bridge "
                      "automatically");
    }

    std::cout << "\nMyBikeControl is ready.\n"
              << "  device name : " << info.name << "\n"
              << "  device id   : " << info.id << "\n"
              << "  TCP port    : " << server.port() << "\n\n"
              << "In MyWhoosh:\n"
              << "  1. Open the Connection screen.\n"
              << "  2. Tap the OpenBikeControl icon (top right) and confirm.\n"
              << "  3. Start riding; shift buttons should change MyShift gears.\n\n"
              << (demo ? "DEMO MODE: no controller needed, gears will cycle every 3 s.\n"
                       : "Waiting for the controller on the BLE side (next phase).\n")
              << "Press Ctrl+C to stop.\n\n";

    auto lastStatus = std::chrono::steady_clock::now();
    int tick = 0;
    int gear = 1;
    bool goingUp = true;

    while (!g_stop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        ++tick;

        if (std::chrono::steady_clock::now() - lastStatus >= std::chrono::seconds(30)) {
            server.sendDeviceStatus();
            lastStatus = std::chrono::steady_clock::now();
        }

        if (demo && tick % 12 == 0) {
            const bool shiftUp = goingUp;
            const uint8_t button =
                shiftUp ? obc::kButtonShiftUp : obc::kButtonShiftDown;

            server.sendButton(button, obc::kStatePressed);
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
            server.sendButton(button, obc::kStateReleased);

            gear += shiftUp ? 1 : -1;
            if (gear >= 8) {
                goingUp = false;
            }
            if (gear <= 1) {
                goingUp = true;
            }

            mybc::logInfo(std::string("demo: ") + (shiftUp ? "shift up" : "shift down") +
                          " -> gear " + std::to_string(gear) +
                          (server.clientCount() == 0 ? " (no app connected)" : ""));
        }
    }

    mdns.stop();
    server.stop();
    mybc::logInfo("stopped");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    std::vector<std::string> args;
    args.reserve(static_cast<size_t>(argc));
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    if (args.empty()) {
        printUsage();
        return 0;
    }

    if (hasOption(args, "--verbose")) {
        mybc::setLogLevel(mybc::LogLevel::Debug);
    }

    const std::string command = args[0];

    if (command == "help" || command == "--help" || command == "-h") {
        printUsage();
        return 0;
    }
    if (command == "version" || command == "--version") {
        std::cout << "MyBikeControl " << kVersion << "\n";
        return 0;
    }
    if (command == "serve") {
        return runServe(args);
    }

    mybc::logError("unknown command: " + command);
    printUsage();
    return 2;
}

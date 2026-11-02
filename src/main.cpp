#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "mybc/ble/ride_bridge.h"
#include "mybc/ble/scanner.h"
#include "mybc/log.h"
#include "mybc/net.h"
#include "mybc/obc/mdns.h"
#include "mybc/obc/server.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace obc = mybc::obc;
namespace ble = mybc::ble;

namespace {

constexpr const char* kVersion = "0.2.0";

volatile std::sig_atomic_t g_stop = 0;

void handleSignal(int) { g_stop = 1; }

void printUsage() {
    std::cout << "MyBikeControl " << kVersion
              << " - Zwift Ride/Play/Click to OpenBikeControl bridge\n\n"
              << "Usage:\n"
              << "  mybc scan    [--seconds N] [--filter TEXT]\n"
              << "  mybc inspect <index|name|address> [--seconds N] [--notify]\n"
              << "  mybc serve   [--port N] [--name TEXT] [--id TEXT] [--controller TEXT]\n"
              << "               [--no-mdns] [--demo]\n"
              << "  mybc version\n"
              << "  mybc help\n\n"
              << "Commands:\n"
              << "  scan     List nearby BLE devices (run this first).\n"
              << "  inspect  Connect to one device and dump its GATT services.\n"
              << "           With --notify, subscribe to every notifying characteristic\n"
              << "           and hexdump the payloads (use this to decode the controller).\n"
              << "  serve    Advertise an OpenBikeControl device and forward shift events.\n"
              << "           --demo sends synthetic shift up/down without hardware.\n\n"
              << "Options:\n"
              << "  --seconds N   Scan/listen duration (default 8).\n"
              << "  --filter S    Case-insensitive name/address filter for scan.\n"
              << "  --port N      TCP port for serve (default 8080, 0 = random).\n"
              << "  --name S      Device name advertised to the trainer app.\n"
              << "  --id S        Unique device id advertised in the mDNS TXT record.\n"
              << "  --controller S  BLE name/address filter (default: Zwift Ride).\n"
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

int positiveInt(const std::optional<std::string>& value, int fallback) {
    if (!value) {
        return fallback;
    }
    const int parsed = std::atoi(value->c_str());
    return parsed > 0 ? parsed : fallback;
}

int runScan(const std::vector<std::string>& args) {
    ble::ScanOptions options;
    options.seconds = positiveInt(optionValue(args, "--seconds"), 8);
    options.filter = optionValue(args, "--filter").value_or("");

    mybc::logInfo("scanning for " + std::to_string(options.seconds) + " s...");
    const std::vector<ble::DeviceSummary> devices = ble::scan(options);

    if (devices.empty()) {
        mybc::logWarn("no devices found. Make sure the controller is awake and not connected "
                      "to Zwift, then try again.");
        return 1;
    }

    std::cout << "\n  #  " << std::left << std::setw(30) << "Name" << std::setw(38)
              << "Address" << std::right << std::setw(5) << "RSSI" << "  Conn  Services\n";

    for (size_t i = 0; i < devices.size(); ++i) {
        const ble::DeviceSummary& device = devices[i];
        std::cout << "  " << std::left << std::setw(3) << i << std::setw(30)
                  << (device.name.empty() ? "-" : device.name) << std::setw(38) << device.address
                  << std::right << std::setw(5) << device.rssi << " dBm  "
                  << (device.connectable ? "yes" : "no ") << "   ";

        for (const std::string& service : device.advertisedServices) {
            std::cout << service << " ";
        }
        std::cout << "\n";
    }

    std::cout << "\nNext: mybc inspect <index> --notify\n";
    return 0;
}

int runInspect(const std::vector<std::string>& args) {
    if (args.size() < 2 || args[1].rfind("--", 0) == 0) {
        mybc::logError("inspect needs a target: index, name or address");
        return 2;
    }

    ble::InspectOptions options;
    options.target = args[1];
    options.seconds = positiveInt(optionValue(args, "--seconds"), 8);
    options.subscribeNotifications = hasOption(args, "--notify");

    return ble::inspect(options) ? 0 : 1;
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

    std::unique_ptr<ble::RideBridge> bridge;
    if (!demo) {
        ble::RideBridgeOptions bridgeOptions;
        if (const auto value = optionValue(args, "--controller")) {
            bridgeOptions.target = *value;
        }

        bridge = std::make_unique<ble::RideBridge>(
            bridgeOptions,
            [&server](const std::vector<ble::ButtonEvent>& events) {
                obc::Bytes pairs;
                for (const ble::ButtonEvent& event : events) {
                    if (!server.appSupports(event.buttonId)) {
                        continue;
                    }
                    pairs.push_back(event.buttonId);
                    pairs.push_back(event.pressed ? obc::kStatePressed : obc::kStateReleased);
                }

                if (pairs.empty()) {
                    return;
                }

                server.sendButtonState(pairs);
                for (size_t i = 0; i + 1 < pairs.size(); i += 2) {
                    mybc::logInfo(std::string("controller: ") + obc::buttonName(pairs[i]) +
                                  (pairs[i + 1] == obc::kStatePressed ? " pressed"
                                                                     : " released"));
                }
            },
            [&server](uint8_t battery) {
                server.setBatteryPercent(battery);
                mybc::logInfo("controller battery: " + std::to_string(battery) + "%");
            });

        if (!bridge->start()) {
            mybc::logWarn("BLE bridge not started; use --demo for synthetic shifts");
            bridge.reset();
        }
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
                       : "BLE: scanning for Zwift Ride controllers; press a button to wake them.\n")
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

    if (bridge) {
        bridge->stop();
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

#if defined(_WIN32)
    // The sources and log strings are UTF-8; make the Windows console match.
    SetConsoleOutputCP(CP_UTF8);
#endif

    if (!mybc::net::initialize()) {
        mybc::logError("failed to initialize the socket layer");
        return 1;
    }

    struct SocketGuard {
        ~SocketGuard() { mybc::net::cleanup(); }
    };
    [[maybe_unused]] SocketGuard socketGuard;

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
    if (command == "scan") {
        return runScan(args);
    }
    if (command == "inspect") {
        return runInspect(args);
    }
    if (command == "serve") {
        return runServe(args);
    }

    mybc::logError("unknown command: " + command);
    printUsage();
    return 2;
}

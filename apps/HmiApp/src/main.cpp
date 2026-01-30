#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "PeakCAN.h"
#include "PCANBasic.h"

namespace {
constexpr uint32_t kCommandId = 0x201U;
constexpr uint32_t kStatusIdBase = 0x101U;
constexpr uint32_t kStatusIdMax = 0x103U;
constexpr int kExitFailure = 2;

enum class DoorState : uint8_t {
    Closed = 0,
    Open = 1,
    Moving = 2,
    Faulted = 3
};

struct Config {
    std::string channel = "PCAN_USBBUS1";
    std::string bitrate = "500k";
};

struct DoorInfo {
    DoorState state = DoorState::Closed;
    uint8_t obstruction = 0;
    uint8_t fault_code = 0;
    std::chrono::steady_clock::time_point last_update{};
};

std::atomic<bool> g_running{true};

#ifdef _WIN32
BOOL WINAPI ConsoleHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_CLOSE_EVENT || type == CTRL_BREAK_EVENT) {
        g_running = false;
        return TRUE;
    }
    return FALSE;
}
#endif

std::string Timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_snapshot{};
#ifdef _WIN32
    localtime_s(&tm_snapshot, &time);
#else
    localtime_r(&time, &tm_snapshot);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_snapshot, "%H:%M:%S");
    return oss.str();
}

void Log(const std::string &message) {
    std::cout << "[" << Timestamp() << "] " << message << std::endl;
}

std::string DoorStateToString(DoorState state) {
    switch (state) {
        case DoorState::Closed:
            return "CLOSED";
        case DoorState::Open:
            return "OPEN";
        case DoorState::Moving:
            return "MOVING";
        case DoorState::Faulted:
            return "FAULTED";
        default:
            return "UNKNOWN";
    }
}

std::string ErrorToString(CANAPI_Return_t rc) {
    switch (rc) {
        case CANERR_NOERROR:
            return "OK";
        case CANERR_RX_EMPTY:
            return "RX_EMPTY";
        case CANERR_TIMEOUT:
            return "TIMEOUT";
        case CPeakCAN::DriverNotLoaded:
            return "PCAN driver not loaded";
        case CPeakCAN::HardwareAlreadyInUse:
            return "PCAN hardware already in use";
        case CPeakCAN::ClientAlreadyConnected:
            return "PCAN client already connected";
        case CPeakCAN::RegisterTestFailed:
            return "PCAN hardware not found";
        default:
            return "CAN error " + std::to_string(rc);
    }
}

bool TryParseChannel(const std::string &text, uint32_t &channel) {
    if (text.rfind("PCAN_USBBUS", 0) == 0) {
        std::string suffix = text.substr(std::string("PCAN_USBBUS").size());
        int index = std::atoi(suffix.c_str());
        static const uint32_t kUsbMap[] = {
            PCAN_USBBUS1,  PCAN_USBBUS2,  PCAN_USBBUS3,  PCAN_USBBUS4,
            PCAN_USBBUS5,  PCAN_USBBUS6,  PCAN_USBBUS7,  PCAN_USBBUS8,
            PCAN_USBBUS9,  PCAN_USBBUS10, PCAN_USBBUS11, PCAN_USBBUS12,
            PCAN_USBBUS13, PCAN_USBBUS14, PCAN_USBBUS15, PCAN_USBBUS16};
        if (index >= 1 && index <= 16) {
            channel = kUsbMap[index - 1];
            return true;
        }
    }

    if (!text.empty() && (std::isdigit(static_cast<unsigned char>(text[0])) || text.rfind("0x", 0) == 0)) {
        try {
            channel = static_cast<uint32_t>(std::stoul(text, nullptr, 0));
            return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

bool ParseArgs(int argc, char **argv, Config &config) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--channel" && i + 1 < argc) {
            config.channel = argv[++i];
        } else if (arg == "--bitrate" && i + 1 < argc) {
            config.bitrate = argv[++i];
        } else {
            std::cerr << "Unknown or incomplete argument: " << arg << std::endl;
            return false;
        }
    }
    return true;
}

CANAPI_Message_t BuildCommandMessage(uint8_t door_id, uint8_t cmd) {
    CANAPI_Message_t message{};
    message.id = kCommandId;
    message.xtd = 0;
    message.rtr = 0;
    message.sts = 0;
    message.dlc = 8;
    message.data[0] = door_id;
    message.data[1] = cmd;
    for (int i = 2; i < 8; ++i) {
        message.data[i] = 0;
    }
    return message;
}

void PrintUsage() {
    std::cout << "HmiApp.exe [--channel PCAN_USBBUS1] [--bitrate 500k]" << std::endl;
}

void PrintMenu() {
    std::cout << "\nCommands:\n"
              << "  1) Open Door 1\n  2) Close Door 1\n  3) Reset Door 1\n"
              << "  4) Open Door 2\n  5) Close Door 2\n  6) Reset Door 2\n"
              << "  7) Open Door 3\n  8) Close Door 3\n  9) Reset Door 3\n"
              << "  q) Quit\n"
              << "> ";
}
}  // namespace

int main(int argc, char **argv) {
    Config config;
    if (!ParseArgs(argc, argv, config)) {
        PrintUsage();
        return kExitFailure;
    }

#ifdef _WIN32
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
#endif

    uint32_t channel = 0;
    if (!TryParseChannel(config.channel, channel)) {
        std::cerr << "Invalid channel string: " << config.channel << std::endl;
        return kExitFailure;
    }

    CANAPI_Bitrate_t bitrate{};
    bool data = false;
    bool sam = false;
    CANAPI_Return_t rc = CPeakCAN::MapString2Bitrate(config.bitrate.c_str(), bitrate, data, sam);
    if (rc != CANERR_NOERROR) {
        std::cerr << "Invalid bitrate string: " << config.bitrate << std::endl;
        return kExitFailure;
    }

    CPeakCAN can_api;
    CANAPI_OpMode_t op_mode{};
    op_mode.byte = static_cast<uint8_t>(CANMODE_DEFAULT | CANMODE_NXTD);

    rc = can_api.InitializeChannel(static_cast<int32_t>(channel), op_mode);
    if (rc != CANERR_NOERROR) {
        std::cerr << "CAN init failed: " << ErrorToString(rc) << std::endl;
        return kExitFailure;
    }

    rc = can_api.StartController(bitrate);
    if (rc != CANERR_NOERROR) {
        std::cerr << "CAN start failed: " << ErrorToString(rc) << std::endl;
        can_api.TeardownChannel();
        return kExitFailure;
    }

    Log("HmiApp started on " + config.channel + " @" + config.bitrate);

    std::mutex door_mutex;
    std::vector<DoorInfo> doors(3);

    std::thread rx_thread([&]() {
        while (g_running.load()) {
            CANAPI_Message_t message{};
            CANAPI_Return_t rc_read = can_api.ReadMessage(message, 100U);
            if (rc_read == CANERR_NOERROR) {
                if (message.sts != 0 || message.xtd != 0 || message.rtr != 0) {
                    continue;
                }
                if (message.id < kStatusIdBase || message.id > kStatusIdMax || message.dlc < 4) {
                    continue;
                }

                uint8_t state_raw = message.data[0];
                uint8_t obstruction = message.data[1];
                uint8_t fault = message.data[2];
                uint8_t door_id = message.data[3];
                if (door_id < 1 || door_id > 3) {
                    door_id = static_cast<uint8_t>((message.id - kStatusIdBase) + 1U);
                }

                DoorState new_state = static_cast<DoorState>(state_raw);
                std::lock_guard<std::mutex> lock(door_mutex);
                DoorInfo &door = doors[door_id - 1];
                bool changed = (door.state != new_state) || (door.obstruction != obstruction) || (door.fault_code != fault);
                door.state = new_state;
                door.obstruction = obstruction;
                door.fault_code = fault;
                door.last_update = std::chrono::steady_clock::now();
                if (changed) {
                    Log("Door " + std::to_string(door_id) + " -> " + DoorStateToString(new_state) +
                        " obs=" + std::to_string(obstruction) + " fault=" + std::to_string(fault));
                }
            } else if (rc_read == CANERR_RX_EMPTY || rc_read == CANERR_TIMEOUT) {
                continue;
            } else {
                Log("CAN read error: " + ErrorToString(rc_read));
            }
        }
    });

    std::thread display_thread([&]() {
        while (g_running.load()) {
            std::vector<DoorInfo> snapshot;
            {
                std::lock_guard<std::mutex> lock(door_mutex);
                snapshot = doors;
            }

            std::cout << "\nDoor Status (STALE if >500ms)" << std::endl;
            std::cout << "ID  STATE     OBS  FAULT  UPDATED" << std::endl;
            auto now = std::chrono::steady_clock::now();
            for (size_t i = 0; i < snapshot.size(); ++i) {
                const DoorInfo &info = snapshot[i];
                bool stale = (info.last_update.time_since_epoch().count() == 0) ||
                             (now - info.last_update > std::chrono::milliseconds(500));
                std::string state = stale ? "STALE" : DoorStateToString(info.state);
                std::cout << "" << (i + 1) << "   " << std::setw(8) << std::left << state << " "
                          << std::setw(4) << std::left << static_cast<int>(info.obstruction) << " "
                          << std::setw(5) << std::left << static_cast<int>(info.fault_code) << " "
                          << (stale ? "-" : "OK") << std::endl;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    });

    std::thread input_thread([&]() {
        while (g_running.load()) {
            PrintMenu();
            std::string line;
            if (!std::getline(std::cin, line)) {
                g_running = false;
                break;
            }
            if (line == "q" || line == "Q") {
                g_running = false;
                break;
            }

            int selection = std::atoi(line.c_str());
            if (selection < 1 || selection > 9) {
                std::cout << "Invalid selection." << std::endl;
                continue;
            }

            uint8_t door_id = static_cast<uint8_t>(((selection - 1) / 3) + 1);
            uint8_t cmd = 0;
            switch ((selection - 1) % 3) {
                case 0:
                    cmd = 1;  // OPEN
                    break;
                case 1:
                    cmd = 2;  // CLOSE
                    break;
                case 2:
                    cmd = 3;  // RESET
                    break;
            }

            CANAPI_Message_t message = BuildCommandMessage(door_id, cmd);
            CANAPI_Return_t rc_write = can_api.WriteMessage(message, 0U);
            if (rc_write != CANERR_NOERROR) {
                Log("CAN write error: " + ErrorToString(rc_write));
            } else {
                std::string cmd_name = (cmd == 1) ? "OPEN" : (cmd == 2) ? "CLOSE" : "RESET_FAULT";
                Log("Sent " + cmd_name + " to door " + std::to_string(door_id));
            }
        }
    });

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    Log("Shutting down...");

    if (input_thread.joinable()) {
        input_thread.join();
    }
    if (display_thread.joinable()) {
        display_thread.join();
    }
    if (rx_thread.joinable()) {
        rx_thread.join();
    }

    can_api.ResetController();
    can_api.TeardownChannel();

    Log("Shutdown complete.");
    return 0;
}

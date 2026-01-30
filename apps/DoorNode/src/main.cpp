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
constexpr int kExitFailure = 2;

enum class DoorState : uint8_t {
    Closed = 0,
    Open = 1,
    Moving = 2,
    Faulted = 3
};

struct Config {
    int door_id = 0;
    std::string channel = "PCAN_USBBUS1";
    std::string bitrate = "500k";
    int period_ms = 100;
    int move_ms = 2000;
    uint8_t obstruction = 0;
};

struct DoorStatus {
    DoorState state = DoorState::Closed;
    uint8_t fault_code = 0;
    uint8_t obstruction = 0;
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
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::tm tm_snapshot{};
#ifdef _WIN32
    localtime_s(&tm_snapshot, &time);
#else
    localtime_r(&time, &tm_snapshot);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_snapshot, "%H:%M:%S") << '.' << std::setw(3) << std::setfill('0')
        << ms.count();
    return oss.str();
}

void Log(const std::string &prefix, const std::string &message) {
    std::cout << "[" << Timestamp() << "] " << prefix << " " << message << std::endl;
}

void LogError(const std::string &prefix, const std::string &message) {
    std::cerr << "[" << Timestamp() << "] " << prefix << " ERROR: " << message << std::endl;
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

struct RateLimiter {
    std::chrono::steady_clock::time_point last_log{};
    size_t suppressed = 0;
};

void LogRateLimited(const std::string &prefix,
                    const std::string &message,
                    RateLimiter &limiter,
                    std::chrono::milliseconds interval) {
    auto now = std::chrono::steady_clock::now();
    if (limiter.last_log.time_since_epoch().count() == 0 ||
        now - limiter.last_log >= interval) {
        std::string suffix;
        if (limiter.suppressed > 0) {
            suffix = " (" + std::to_string(limiter.suppressed) + " similar errors suppressed)";
            limiter.suppressed = 0;
        }
        Log(prefix, message + suffix);
        limiter.last_log = now;
    } else {
        ++limiter.suppressed;
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
        if (arg == "--id" && i + 1 < argc) {
            config.door_id = std::atoi(argv[++i]);
        } else if (arg == "--channel" && i + 1 < argc) {
            config.channel = argv[++i];
        } else if (arg == "--bitrate" && i + 1 < argc) {
            config.bitrate = argv[++i];
        } else if (arg == "--period_ms" && i + 1 < argc) {
            config.period_ms = std::atoi(argv[++i]);
        } else if (arg == "--move_ms" && i + 1 < argc) {
            config.move_ms = std::atoi(argv[++i]);
        } else if (arg == "--obstruction" && i + 1 < argc) {
            config.obstruction = static_cast<uint8_t>(std::atoi(argv[++i]));
        } else {
            std::cerr << "Unknown or incomplete argument: " << arg << std::endl;
            return false;
        }
    }

    if (config.door_id < 1 || config.door_id > 3) {
        std::cerr << "--id must be 1..3" << std::endl;
        return false;
    }

    if (config.period_ms <= 0 || config.move_ms <= 0) {
        std::cerr << "--period_ms and --move_ms must be > 0" << std::endl;
        return false;
    }

    if (config.obstruction > 1) {
        std::cerr << "--obstruction must be 0 or 1" << std::endl;
        return false;
    }

    return true;
}

CANAPI_Message_t BuildStatusMessage(int door_id, const DoorStatus &status) {
    CANAPI_Message_t message{};
    message.id = kStatusIdBase + static_cast<uint32_t>(door_id - 1);
    message.xtd = 0;
    message.rtr = 0;
    message.sts = 0;
    message.dlc = 8;
    message.data[0] = static_cast<uint8_t>(status.state);
    message.data[1] = status.obstruction;
    message.data[2] = status.fault_code;
    message.data[3] = static_cast<uint8_t>(door_id);
    for (int i = 4; i < 8; ++i) {
        message.data[i] = 0;
    }
    return message;
}

void PrintUsage() {
    std::cout << "DoorNode.exe --id <1..3> [--channel PCAN_USBBUS1] [--bitrate 500k]"
              << " [--period_ms 100] [--move_ms 2000] [--obstruction 0|1]" << std::endl;
}
}  // namespace

int main(int argc, char **argv) {
    Config config;
    if (!ParseArgs(argc, argv, config)) {
        PrintUsage();
        return kExitFailure;
    }
    const std::string log_prefix = "DoorNode[" + std::to_string(config.door_id) + "]";

#ifdef _WIN32
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
#endif

    uint32_t channel = 0;
    if (!TryParseChannel(config.channel, channel)) {
        LogError(log_prefix, "Invalid channel string: " + config.channel);
        return kExitFailure;
    }

    CANAPI_Bitrate_t bitrate{};
    bool data = false;
    bool sam = false;
    CANAPI_Return_t rc = CPeakCAN::MapString2Bitrate(config.bitrate.c_str(), bitrate, data, sam);
    if (rc != CANERR_NOERROR) {
        LogError(log_prefix, "Invalid bitrate string: " + config.bitrate);
        return kExitFailure;
    }

    CPeakCAN can_api;
    CANAPI_OpMode_t op_mode{};
    op_mode.byte = static_cast<uint8_t>(CANMODE_DEFAULT | CANMODE_NXTD);

    rc = can_api.InitializeChannel(static_cast<int32_t>(channel), op_mode);
    if (rc != CANERR_NOERROR) {
        LogError(log_prefix, "CAN init failed: " + ErrorToString(rc));
        LogError(log_prefix, "Check that the PCAN driver is installed, the channel is valid, and not already in use.");
        return kExitFailure;
    }

    rc = can_api.StartController(bitrate);
    if (rc != CANERR_NOERROR) {
        LogError(log_prefix, "CAN start failed: " + ErrorToString(rc));
        LogError(log_prefix, "Bitrate mismatch or CAN init failure. Verify the bus is at " + config.bitrate + ".");
        can_api.TeardownChannel();
        return kExitFailure;
    }

    Log(log_prefix, "CAN init OK on " + config.channel + " @" + config.bitrate);
    Log(log_prefix, "DoorNode started for door " + std::to_string(config.door_id));

    std::mutex status_mutex;
    DoorStatus status;
    status.obstruction = config.obstruction;
    std::atomic<uint64_t> move_token{0};
    RateLimiter read_limiter;
    RateLimiter write_limiter;

    auto set_state = [&](DoorState next) {
        std::lock_guard<std::mutex> lock(status_mutex);
        if (status.state != next) {
            status.state = next;
            Log(log_prefix, "Door state -> " + DoorStateToString(next));
        }
    };

    auto set_fault = [&](uint8_t fault_code) {
        std::lock_guard<std::mutex> lock(status_mutex);
        if (status.fault_code != fault_code) {
            status.fault_code = fault_code;
            Log(log_prefix, "Fault code -> " + std::to_string(fault_code));
        }
    };

    auto start_move = [&](DoorState target) {
        uint64_t token = ++move_token;
        set_state(DoorState::Moving);
        std::thread([&, token, target]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(config.move_ms));
            if (!g_running.load()) {
                return;
            }
            if (move_token.load() != token) {
                return;
            }
            set_state(target);
        }).detach();
    };

    std::thread rx_thread([&]() {
        while (g_running.load()) {
            CANAPI_Message_t message{};
            CANAPI_Return_t rc_read = can_api.ReadMessage(message, 100U);
            if (rc_read == CANERR_NOERROR) {
                if (message.sts != 0 || message.xtd != 0 || message.rtr != 0) {
                    continue;
                }
                if (message.id != kCommandId || message.dlc < 2) {
                    continue;
                }
                uint8_t door_id = message.data[0];
                uint8_t cmd = message.data[1];
                if (door_id != config.door_id) {
                    continue;
                }

                if (cmd == 1) {
                    DoorState current_state;
                    {
                        std::lock_guard<std::mutex> lock(status_mutex);
                        current_state = status.state;
                    }
                    if (current_state == DoorState::Closed) {
                        Log(log_prefix, "Command OPEN received");
                        start_move(DoorState::Open);
                    }
                } else if (cmd == 2) {
                    DoorState current_state;
                    {
                        std::lock_guard<std::mutex> lock(status_mutex);
                        current_state = status.state;
                    }
                    if (current_state == DoorState::Open) {
                        Log(log_prefix, "Command CLOSE received");
                        start_move(DoorState::Closed);
                    }
                } else if (cmd == 3) {
                    Log(log_prefix, "Command RESET_FAULT received");
                    set_fault(0);
                    set_state(DoorState::Closed);
                }
            } else if (rc_read == CANERR_RX_EMPTY || rc_read == CANERR_TIMEOUT) {
                continue;
            } else {
                LogRateLimited(log_prefix, "CAN read error: " + ErrorToString(rc_read), read_limiter,
                               std::chrono::milliseconds(1000));
            }
        }
    });

    std::thread tx_thread([&]() {
        auto next_tick = std::chrono::steady_clock::now();
        auto last_alive = std::chrono::steady_clock::now();
        while (g_running.load()) {
            DoorStatus snapshot;
            {
                std::lock_guard<std::mutex> lock(status_mutex);
                snapshot = status;
            }

            CANAPI_Message_t msg = BuildStatusMessage(config.door_id, snapshot);
            CANAPI_Return_t rc_write = can_api.WriteMessage(msg, 0U);
            if (rc_write != CANERR_NOERROR) {
                LogRateLimited(log_prefix, "CAN write error: " + ErrorToString(rc_write), write_limiter,
                               std::chrono::milliseconds(1000));
            }

            auto now = std::chrono::steady_clock::now();
            if (now - last_alive >= std::chrono::seconds(1)) {
                Log(log_prefix, "Alive: state=" + DoorStateToString(snapshot.state));
                last_alive = now;
            }

            // Timing assumption: steady_clock + sleep_until keeps the TX period stable
            // within acceptable jitter for demo purposes.
            next_tick += std::chrono::milliseconds(config.period_ms);
            std::this_thread::sleep_until(next_tick);
        }
    });

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    Log(log_prefix, "Shutting down...");

    if (rx_thread.joinable()) {
        rx_thread.join();
    }
    if (tx_thread.joinable()) {
        tx_thread.join();
    }

    can_api.ResetController();
    can_api.TeardownChannel();

    Log(log_prefix, "Shutdown complete.");
    return 0;
}

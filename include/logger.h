#pragma once

#include <string>
#include <iostream>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace oms {

class Logger {
public:
    enum Level { INFO, WARN, ERROR, DEBUG };

    static void log(Level level, const std::string& message) {
        std::string levelStr;
        switch (level) {
            case INFO: levelStr = "INFO"; break;
            case WARN: levelStr = "WARN"; break;
            case ERROR: levelStr = "ERROR"; break;
            case DEBUG: levelStr = "DEBUG"; break;
        }

        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::stringstream ss;
        ss << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S")
           << '.' << std::setfill('0') << std::setw(3) << ms.count()
           << " [" << levelStr << "] " << message;

        std::cout << ss.str() << std::endl;
    }

    static void info(const std::string& msg) { log(INFO, msg); }
    static void warn(const std::string& msg) { log(WARN, msg); }
    static void error(const std::string& msg) { log(ERROR, msg); }
    static void debug(const std::string& msg) { log(DEBUG, msg); }
};

}

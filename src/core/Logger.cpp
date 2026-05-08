#include "core/Logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <ostream>
#include <sstream>

namespace rr::core {

namespace {

std::mutex g_log_mutex;

std::string timestamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto t   = system_clock::to_time_t(now);
    const auto ms  = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    std::ostringstream os;
    os << std::put_time(&tm, "%H:%M:%S") << '.'
       << std::setw(3) << std::setfill('0') << ms.count();
    return os.str();
}

void emit(std::ostream& out, std::string_view level, std::string_view message) {
    std::lock_guard lock(g_log_mutex);
    out << '[' << timestamp() << "] [" << level << "] " << message << '\n';
    out.flush();
}

}

void Logger::info(std::string_view message) {
    emit(std::cout, "INFO", message);
}

void Logger::warning(std::string_view message) {
    emit(std::cerr, "WARN", message);
}

void Logger::error(std::string_view message) {
    emit(std::cerr, "ERROR", message);
}

}

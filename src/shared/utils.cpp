#include "utils.hpp"

#include <iostream>

std::string humanReadableSize ( const size_t size ) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    auto sizeDouble = static_cast<double>(size);
    size_t unitIndex = 0;

    // Calculate the appropriate unit
    while ( sizeDouble >= 1000.0 && unitIndex < std::size(units) - 1 ) {
        sizeDouble /= 1000.0;
        unitIndex++;
    }

    // Format the size to 2 decimal places and append unit
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << sizeDouble << " " << units[unitIndex];
    return oss.str();
}

std::string humanReadableSpeed ( double speed ) {
    const char* units[] = {"B/s", "KB/s", "MB/s", "GB/s", "TB/s"};
    size_t unitIndex = 0;

    // Calculate the appropriate unit
    while ( speed >= 1000.0 && unitIndex < std::size(units) - 1 ) {
        speed /= 1000.0;
        unitIndex++;
    }

    // Format the speed to 2 decimal places and append unit
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << speed << " " << units[unitIndex];
    return oss.str();
}

unsigned long getFreeMemory () {
    struct sysinfo memInfo{};
    sysinfo(&memInfo);
    return memInfo.bufferram + memInfo.freeram;
}

std::string padStringToSize ( const std::string& str, const unsigned totalLength ) {
    if ( str.size() >= totalLength )
        return str;
    return str + std::string(totalLength - str.size(), ' ');
}

std::string bytesToHex(const unsigned char* data, const size_t length) {
    static constexpr char hexDigits[] = "0123456789abcdef";
    std::string hexStr;
    hexStr.reserve(length * 2);
    for (size_t i = 0; i < length; ++i) {
        hexStr.push_back(hexDigits[(data[i] >> 4) & 0x0F]);
        hexStr.push_back(hexDigits[data[i] & 0x0F]);
    }

    return hexStr;
}

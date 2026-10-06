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
    if (sysinfo(&memInfo) != 0) return 0;
    unsigned long totalFree = memInfo.freeram + memInfo.bufferram + memInfo.totalhigh;
#ifdef __linux__
    // On Linux, we should also consider cached memory if possible, 
    // but sysinfo doesn't directly provide 'cached'.
    // However, freeram + bufferram is a better start than just freeram.
#endif
    return totalFree * memInfo.mem_unit;
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

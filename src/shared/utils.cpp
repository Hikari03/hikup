#include "utils.hpp"

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

std::string binToHex ( const unsigned char* bin, const size_t size ) {
    const auto hex = std::make_unique<char[]>(size * 2 + 1);

    sodium_bin2hex(hex.get(), size * 2 + 1, bin, size);

    return {hex.get(), size * 2};
}

 std::pair<unsigned char*, size_t> hexToBin ( const std::string& hex ) {
    const auto bin = std::make_unique<unsigned char[]>(hex.size() / 2);

    sodium_hex2bin(bin.get(), hex.size() / 2, hex.c_str(), hex.size(), nullptr, nullptr, nullptr);

    return {bin.get(), hex.size() / 2};
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
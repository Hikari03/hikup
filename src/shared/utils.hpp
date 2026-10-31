#pragma once

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <ios>
#include <memory>
#include <string>
#include <sys/sysinfo.h>
#include <vector>

std::string humanReadableSize ( size_t size );

std::string humanReadableSpeed ( double speed );

unsigned long getFreeMemory ();

std::string padStringToSize ( const std::string& str, unsigned totalLength );

std::string bytesToHex(const unsigned char* data, size_t length);

/**
 * @brief Send buffer with adaptive chunk sizing
 *
 * Starts small so transfers become responsive quickly (early progress,
 * small memory footprint) and grows while the link is fast. The equilibrium
 * chunk duration is ~40–100 ms — per-write overhead at these sizes is
 * negligible, so throughput is not compromised.
 */
class AdaptiveBuffer {
public:
    explicit AdaptiveBuffer ( const size_t initialSize = 256 * 1024, const size_t minSize = 256 * 1024 )
        : _buffer(initialSize), _minSize(minSize) {}

    [[nodiscard]] char* data () { return _buffer.data(); }
    [[nodiscard]] size_t size () const { return _buffer.size(); }

    /**
     * @brief Feed the duration of the last chunk send and adjust the size
     *
     * @param seconds duration of the last send
     * @param freeRam free memory allowed for growing the buffer
     */
    void reportSendDuration ( const double seconds, const size_t freeRam ) {
        size_t newSize = _buffer.size();

        if ( seconds > 0.1 )
            newSize = std::max(_minSize, static_cast<size_t>(_buffer.size() * 0.75));
        else if ( seconds < 0.02 && freeRam >= _buffer.size() * 2 )
            newSize = _buffer.size() * 2;
        else if ( seconds < 0.04 && freeRam >= _buffer.size() * 2 )
            newSize = static_cast<size_t>(_buffer.size() * 1.25);

        if ( newSize != _buffer.size() )
            _buffer.resize(newSize);
    }

private:
    std::vector<char> _buffer;
    size_t _minSize;
};
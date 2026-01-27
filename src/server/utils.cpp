#include "utils.hpp"

#include <iostream>
#include <mutex>

std::mutex _logMutex;

namespace Utils {
    void log ( const std::string& message, const bool newline ) {
        std::lock_guard lock(_logMutex);
        std::cout << message;
        if ( newline )
            std::cout << std::endl;
        else
            std::cout << std::flush;
    }

    void elog ( const std::string& message, const bool newline ) {
        std::lock_guard lock(_logMutex);
        std::cerr << message;
        if ( newline )
            std::cerr << std::endl;
        else
            std::cerr << std::flush;
    }
}
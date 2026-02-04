#include "utils.hpp"

#include <iostream>
#include <mutex>
#include <set>


std::mutex _logMutex;

std::string operator+ ( const std::string& lhs, const toml::source_position& rhs ) {
    std::stringstream rhs1;
    rhs1 << lhs << rhs;
    return rhs1.str();
}

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



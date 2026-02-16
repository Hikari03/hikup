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

    namespace FS {





        template < SetOrVectorOfString T >
        T findCorrespondingFileNames ( const std::set<std::string>& toFind ) {
            const auto directory = std::filesystem::current_path() / "storage";

            T result;

            for ( const auto& entry: std::filesystem::directory_iterator(directory) ) {
                if ( toFind.contains(entry.path().extension().string().substr(1)) ) {
                    result.emplace(entry.path().filename().string());
                }
            }

            return result;
        }

        template < SetOrVectorOfString T >
        T _getLocalFileHashes () {
            T hashes;

            for ( const auto directory = std::filesystem::current_path() / "storage";
                const auto& file: std::filesystem::directory_iterator(directory) ) {

                hashes.emplace(file.path().extension().string().substr(1));
                }

            return hashes;
        }
    }
}



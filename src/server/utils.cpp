#include "utils.hpp"

#include <algorithm>
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


        std::set<std::string> findCorrespondingFileNames ( const std::set<std::string>& hash ) {
            const auto directory = std::filesystem::current_path() / "storage";

            std::set<std::string> result;

            for ( const auto& entry: std::filesystem::directory_iterator(directory) ) {
                if ( hash.contains(entry.path().extension().string().substr(1)) ) {
                    result.emplace(entry.path().filename().string());
                }
            }

            return result;
        }

        std::optional<std::string> findCorrespondingFileName ( const std::string& hash ) {
            const auto directory = std::filesystem::current_path() / "storage";

            for ( const auto& entry: std::filesystem::directory_iterator(directory) ) {
                if ( hash == entry.path().extension().string().substr(1) ) {
                    return entry.path().filename().string();
                }
            }

            return {};
        }

        std::set<std::string> getLocalFileHashes () {
            std::set<std::string> hashes;

            for ( const auto directory = std::filesystem::current_path() / "storage";
                const auto& file: std::filesystem::directory_iterator(directory) ) {

                hashes.emplace(file.path().extension().string().substr(1));
            }

            return hashes;
        }

        std::optional<std::string> getLocalRawFileName ( std::string filename ) {

            std::ranges::replace(filename, '.', '<');

            for ( const auto directory = std::filesystem::current_path() / "storage";
                const auto& file: std::filesystem::directory_iterator(directory) ) {

                if ( file.path().stem() == filename )
                    return file.path().filename().string();
            }

            return {};
        }
    }
}



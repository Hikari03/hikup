#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace Utils {
    void log ( const std::string& message, bool newline = true );
    void elog ( const std::string& message, bool newline = true );

    [[nodiscard]] bool addIntoArrInToml ( const std::filesystem::path& path, const std::vector<std::string>& additions);
    [[nodiscard]] bool addIntoArrInToml ( const std::filesystem::path& path, const std::string& addition);
}

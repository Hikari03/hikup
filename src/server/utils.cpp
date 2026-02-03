#include "utils.hpp"

#include <iostream>
#include <mutex>

#include "includes/toml.hpp"

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

    bool addIntoArrInToml ( const std::filesystem::path& path, const std::vector<std::string>& additions ) {
        toml::table root;

        // Try to parse the existing TOML file, if it exists and is non-empty
        {
            if ( std::ifstream file(path);
                file && file.peek() != std::ifstream::traits_type::eof()) {
                try {
                    auto tbl = toml::parse_file(path.string());
                    root = std::move(tbl);
                }
                catch (const toml::parse_error& err) {
                    std::cerr << "Failed to parse TOML file: " << err << "\n";
                    return false;
                }
            }
        }

        // Access the array of strings, or create it if it doesn't exist
        toml::array* arr = nullptr;

        // Check if the key "my_strings" exists and is an array
        if (auto val = root["toRemove"]; val)
        {
            if (auto existing_arr = val.as_array())
            {
                arr = existing_arr;
            }
            else
            {
                std::cerr << "\"toRemove\" exists but is not an array\n";
                return false;
            }
        }
        else
        {
            // Create the array in root table
            root.insert("toRemove", toml::array{});
            arr = root["toRemove"].as_array();
        }

        if (!arr)
        {
            std::cerr << "Failed to get or create the array\n";
            return false;
        }

        // Append new strings
        for (const auto& toAdd : additions)
            arr->push_back(toAdd);

        // Serialize and save back
        std::ofstream out(path, std::ios::trunc);
        if (!out)
        {
            std::cerr << "Cannot open file for writing\n";
            return false;
        }
        out << root;

        out.close();
        return true;
    }

    bool addIntoArrInToml ( const std::filesystem::path& path, const std::string& addition ) {
        toml::table root;

        // Try to parse the existing TOML file, if it exists and is non-empty
        {
            if ( std::ifstream file(path);
                file && file.peek() != std::ifstream::traits_type::eof()) {
                try {
                    auto tbl = toml::parse_file(path.string());
                    root = std::move(tbl);
                }
                catch (const toml::parse_error& err) {
                    std::cerr << "Failed to parse TOML file: " << err << "\n";
                    return false;
                }
                }
        }

        // Access the array of strings, or create it if it doesn't exist
        toml::array* arr = nullptr;

        // Check if the key "my_strings" exists and is an array
        if (auto val = root["toRemove"]; val)
        {
            if (auto existing_arr = val.as_array())
            {
                arr = existing_arr;
            }
            else
            {
                std::cerr << "\"toRemove\" exists but is not an array\n";
                return false;
            }
        }
        else
        {
            // Create the array in root table
            root.insert("toRemove", toml::array{});
            arr = root["toRemove"].as_array();
        }

        if (!arr)
        {
            std::cerr << "Failed to get or create the array\n";
            return false;
        }

        // Append new string
        arr->push_back(addition);

        // Serialize and save back
        std::ofstream out(path, std::ios::trunc);
        if (!out)
        {
            std::cerr << "Cannot open file for writing\n";
            return false;
        }
        out << root;

        out.close();
        return true;
    }
}

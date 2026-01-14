#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <utility>

class FileInfo {
public:
    FileInfo (
        std::string name,
        std::string hash,
        std::size_t size,
        const std::chrono::time_point<std::chrono::utc_clock>& creationDate
    );

    FileInfo ( const std::filesystem::path &filePath, bool convertFromServerNameScheme = false );

    explicit FileInfo ( std::string encodedRepresentation );

    [[nodiscard]] std::string getName () const;

    [[nodiscard]] std::string getHash () const;

    [[nodiscard]] std::size_t getSize () const;

    [[nodiscard]] std::chrono::time_point<std::chrono::utc_clock> getCreationDateUTC () const;

    [[nodiscard]] std::chrono::time_point<std::chrono::system_clock> getCreationDateInTZ () const;

    [[nodiscard]] std::string getCreationDateString_c ();

    [[nodiscard]] std::string encode () const;

private:
    std::string _name;
    std::string _hash;
    std::string _uploadDateString_A_cCache;
    std::size_t _size;
    std::chrono::time_point<std::chrono::utc_clock> _creationDate;
};

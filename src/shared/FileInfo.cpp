#include "FileInfo.hpp"

#include <algorithm>

FileInfo::FileInfo(
    std::string name,
    std::string hash,
    const std::size_t size,
    const std::chrono::time_point<std::chrono::utc_clock>& creationDate
) : _name(std::move(name)), _hash(std::move(hash)), _size(size), _creationDate(creationDate) {}

FileInfo::FileInfo ( const std::filesystem::path& filePath, const bool convertFromServerNameScheme ) {
    if ( !std::filesystem::exists(filePath) )
        throw std::runtime_error("File does not exist: " + filePath.string());

    if ( !std::filesystem::is_regular_file(filePath) )
        throw std::runtime_error("Path is not a regular file: " + filePath.string());

    _name = filePath.filename().string();

    if ( convertFromServerNameScheme ) {
        // convert all '<' to '.'
        _name = filePath.stem();
        std::ranges::replace(_name, '<', '.');
    }

    _hash = filePath.extension().string().substr(1); // remove the leading '.'

    _size = std::filesystem::file_size(filePath);

    const auto ftime = std::filesystem::last_write_time(filePath);
    _creationDate = std::chrono::clock_cast<std::chrono::utc_clock>(ftime);
}

FileInfo::FileInfo ( std::string encodedRepresentation ) {
    if ( encodedRepresentation.empty() )
        throw std::runtime_error("Cannot decode empty FileInfo representation");


    // formatted as name|hash|size|uploadDate

    for ( size_t i = 0; i < 4; ++i ) {
        const auto delimiterPos = encodedRepresentation.find('|');
        if ( delimiterPos == std::string::npos )
            throw std::runtime_error("Invalid FileInfo representation: " + encodedRepresentation);

        const auto token = encodedRepresentation.substr(0, delimiterPos);

        switch ( i ) {
            case 0:
                _name = token;
                break;
            case 1:
                _hash = token;
                break;
            case 2:
                try {
                    _size = std::stoull(token);
                } catch ( const std::exception& e ) {
                    throw std::runtime_error("Invalid size in FileInfo representation: " + token + " | " + e.what());
                }
                break;
            case 3:
                try {
                    const auto timeInSeconds = std::stol(token);
                    _creationDate = std::chrono::time_point<std::chrono::utc_clock>(std::chrono::seconds(timeInSeconds));
                } catch ( const std::exception& e) {
                    throw std::runtime_error("Invalid upload date in FileInfo representation: " + token + " | " + e.what());
                }
            default:
                break;
        }

        encodedRepresentation = encodedRepresentation.substr(delimiterPos + 1);
    }

    if ( !encodedRepresentation.empty() && encodedRepresentation != "#" )
        throw std::runtime_error("Invalid FileInfo representation, extra data found: " + encodedRepresentation);

}

std::string FileInfo::getName () const {
    return _name;
}

std::string FileInfo::getHash () const {
    return _hash;
}

std::size_t FileInfo::getSize () const {
    return _size;
}

std::chrono::time_point<std::chrono::utc_clock> FileInfo::getCreationDateUTC () const {
    return _creationDate;
}

std::chrono::time_point<std::chrono::system_clock> FileInfo::getCreationDateInTZ () const {
    return std::chrono::clock_cast<std::chrono::system_clock>(_creationDate);
}

std::string FileInfo::getCreationDateString_c () {
    if ( !_uploadDateString_A_cCache.empty() )
        return _uploadDateString_A_cCache;

    const std::time_t t_c = std::chrono::system_clock::to_time_t(getCreationDateInTZ());
    char mbstr[100];
    if ( !std::strftime(mbstr, sizeof(mbstr), "%c", std::localtime(&t_c)) ) {
        throw std::runtime_error("Could not format upload date");
    }

    _uploadDateString_A_cCache = std::string(mbstr);

    return _uploadDateString_A_cCache;
}

std::string FileInfo::encode () const {
    std::string ret = _name + '|' + _hash + '|' + std::to_string(_size) + '|';

    ret += std::to_string(std::chrono::duration_cast<std::chrono::seconds>(_creationDate.time_since_epoch()).count()) + '|';

    return ret + '#'; // '#' as end marker
}

#include "ConnectionHandler.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <set>
#include <utility>

#include "utils.hpp"
#include "../shared/FileInfo.hpp"
#include "../shared/utils.hpp"



ConnectionHandler::ConnectionHandler ( Settings settings )
    : settings(std::move(settings)) {
    if (!settings.syncTargets.empty())
        syncThread = std::jthread(&ConnectionHandler::_syncer, this);
}

void ConnectionHandler::addClient ( ClientInfo client ) {
    clientThreads.emplace_back(&ConnectionHandler::_serveConnection, this, std::move(client));
}


void ConnectionHandler::_serveConnection ( ClientInfo client ) const {
    Utils::log("ConnectionHandler: serving client " + client.getIp());

    ConnectionServer connection(client);

    try {
        connection.init();
        const auto message = connection.receiveInternal();

        Utils::log("ConnectionHandler: received message: " + message);
        if ( message == "command:UPLOAD" )
            _receiveFile(connection);
        else if ( message == "command:DOWNLOAD" )
            _sendFile(connection);
        else if ( message == "command:REMOVE" )
            _removeFile(connection);
        else if ( message == "command:LIST" )
            _listFiles(connection);
        else if ( message == "command:SYNC" )
            _syncAsSlave(connection);
    }
    catch ( const std::exception& e ) {
        Utils::elog("ConnectionHandler: error serving client: " + std::string(e.what()));
    }
}

bool ConnectionHandler::_auth ( const std::string& user, const std::string& pass ) const {
    if ( !user.starts_with("user:") )
        throw std::runtime_error("listFiles: no user specified, got: " + user);

    const auto userCut = user.substr(strlen("user:"));

    if ( !pass.starts_with("pass:") )
        throw std::runtime_error("listFiles: no pass specified, got: " + pass);

    const auto passCut = pass.substr(strlen("user:"));

    if ( userCut != settings.authUser || passCut != settings.authPass ) {
        Utils::log("Wrong Credentials: " + userCut + ", " + passCut);
        return false;
    }

    return true;
}

void ConnectionHandler::_receiveFile ( ConnectionServer& connection ) const {
    const auto fileSize = stoll(connection.receiveInternal().substr(strlen("size:")));
    auto fileName = connection.receiveInternal().substr(strlen("filename:"));
    const auto hashFromClient = connection.receiveInternal().substr(strlen("hash:"));

    const auto oldFileName = fileName;

    const auto freeRam = std::min(getFreeMemory() / 4, static_cast<unsigned long>(fileSize / 16));

    connection.resizeBuffer(freeRam);

    // convert all '.' to '$' in the filename
    std::ranges::replace(fileName, '.', '<');

    std::filesystem::path _path = std::filesystem::current_path() / "storage" / ( fileName + '.' + hashFromClient );

    if ( std::filesystem::exists(_path) ) {
        Utils::log("receiveFile: file already exists");
        connection.sendInternal("NO");
        connection.sendInternal(settings.httpProtocol + "://" + settings.hostname + "/" + oldFileName);
        return;
    }

    connection.sendInternal("OK");

    std::ofstream file(_path, std::ios::binary);

    Utils::log("receiveFile: starting download of size: " + std::to_string(fileSize));

    std::string message;
    long long sizeWritten = 0;

    unsigned char hash[crypto_generichash_BYTES];
    crypto_generichash_state state;
    crypto_generichash_init(&state, nullptr, 0, sizeof hash);

    while ( true ) {
        try { message = connection.receive(); }
        catch ( const std::exception& e ) {
            std::cerr << "receiveFile: error receiving message: " << e.what() << std::endl;
            file.close();
            std::filesystem::remove(_path);
            connection.sendInternal("fail");
            return;
        }

        if ( message.starts_with(_internal"DONE") )
            break;

        file.write(message.data(), message.size());
        sizeWritten += message.size();

        connection.sendInternal("confirm");

        crypto_generichash_update(&state, reinterpret_cast<const unsigned char*>(message.data()), message.size());

        Utils::log(
            std::string("\r") + "main: " + humanReadableSize(sizeWritten) + " / " + humanReadableSize(fileSize) +
            " bytes written", false
        );
    }
    std::cout << std::endl;
    file.close();

    crypto_generichash_final(&state, hash, sizeof hash);

    auto hashString = binToHex(hash, sizeof hash);

    if ( hashFromClient != hashString ) {
        std::filesystem::remove(_path);
        connection.sendInternal("Sent hash and calculated hash do not match");
        Utils::elog("Sent hash and calculated hash do not match");
    }

    connection.sendInternal("OK");

    std::filesystem::create_symlink(_path, std::filesystem::current_path() / "links" / oldFileName);

    connection.sendInternal(hashString);
    connection.sendInternal(std::to_string(settings.wantHttp));
    if ( settings.wantHttp )
        connection.sendInternal(settings.httpProtocol + "://" + settings.hostname + "/" + oldFileName);
}

void ConnectionHandler::_sendFile ( ConnectionServer& connection ) {
    auto hash = connection.receiveInternal().substr(strlen("hash:"));
    std::string fileName;

    for ( const auto& file: std::filesystem::directory_iterator("storage") )
        if ( file.path().extension() == '.' + hash )
            fileName = file.path();

    if ( fileName.empty() ) {
        Utils::log("sendFile: file not found");
        connection.sendInternal("NO");
        return;
    }

    std::ifstream file(fileName, std::ios::binary);

    if ( !file.good() ) {
        Utils::log("sendFile: could not open file");
        connection.sendInternal("NO");
        return;
    }

    const auto freeRam = getFreeMemory() / 4;

    connection.sendInternal("OK");

    const auto fileSize = std::filesystem::file_size(fileName);

    size_t chunkSize = 2 * 1024 * 1024;
    auto buffer = std::make_unique<char[]>(chunkSize);

    connection.sendInternal(std::to_string(fileSize));

    auto lastOfSlash = fileName.find_last_of('/');
    auto lastOfDot = fileName.find_last_of('.');

    auto clientFileName = fileName.substr(lastOfSlash + 1, lastOfDot - lastOfSlash - 1);
    std::ranges::replace(clientFileName, '<', '.');

    connection.sendInternal(clientFileName);

    Utils::log("sendFile: starting upload of size: " + humanReadableSize(fileSize));

    size_t sizeRead = 0;

    while ( true ) {
        file.read(buffer.get(), chunkSize);

        const auto startUploadTime = std::chrono::high_resolution_clock::now();
        connection.send(std::string(buffer.get(), file.gcount()));
        const auto endUploadTime = std::chrono::high_resolution_clock::now();

        std::chrono::duration<double> duration = endUploadTime - startUploadTime;

        sizeRead += file.gcount();

        if ( connection.receiveInternal() != "confirm" )
            throw std::runtime_error("sendFile: client did not confirm the chunk");

        if ( sizeRead == static_cast<unsigned long long>(fileSize) )
            break;

        // adjust chunk size based on duration
        if ( duration.count() > 1.4 ) {
            // decrease chunkSize by 2%, ensure integer rounding
            chunkSize = static_cast<int>(chunkSize * 0.75);

            buffer = std::make_unique<char[]>(chunkSize);
        }
        else if ( duration.count() < 0.3 && freeRam >= chunkSize * 2 ) {
            chunkSize = static_cast<int>(chunkSize * 2);
            buffer = std::make_unique<char[]>(chunkSize);
        }
        else if ( duration.count() < 0.6 && freeRam >= chunkSize * 2 ) {
            chunkSize = static_cast<int>(chunkSize * 1.25);
            buffer = std::make_unique<char[]>(chunkSize);
        }
    }

    connection.sendInternal("DONE");
}

void ConnectionHandler::_removeFile ( ConnectionServer& connection ) {
    const auto hash = connection.receiveInternal().substr(strlen("hash:"));

    std::filesystem::path fileName;

    for ( const auto& file: std::filesystem::directory_iterator("storage") )
        if ( file.path().extension() == '.' + hash )
            fileName = file.path();

    if ( fileName.empty() ) {
        Utils::log("removeFile: file not found: " + fileName.string());
        connection.sendInternal("NO");
        return;
    }

    auto symlinkName = fileName.filename().string().substr(0, fileName.filename().string().find('.'));
    std::ranges::replace(symlinkName, '<', '.');


    Utils::log(
        "removeFile: removing files: " + ( std::filesystem::current_path() / "links" / symlinkName ).string() + ", " +
        fileName.string()
    );

    std::filesystem::remove(std::filesystem::current_path() / "links" / symlinkName);
    std::filesystem::remove(fileName);

    connection.sendInternal("OK");
}

void ConnectionHandler::_listFiles ( ConnectionServer& connection ) const {
    connection.sendInternal("OK");

    const std::string user = connection.receiveInternal();
    const std::string pass = connection.receiveInternal();

    if ( !_auth(user, pass) ) {
        connection.sendInternal("NOPE");
        return;
    }

    connection.sendInternal("OK");

    for ( const auto& file: std::filesystem::directory_iterator("storage") ) {
        connection.sendData(FileInfo(file, true).encode());
    }

    connection.sendInternal("DONE");
}

void ConnectionHandler::_sendFileInSync ( ConnectionServer& connection, const std::string& fileName ) {
    const auto _path = std::filesystem::current_path() / "storage" / fileName;

    const auto fileSize = std::filesystem::file_size(_path);

    const auto lastOfSlash = fileName.find_last_of('/');
    const auto lastOfDot = fileName.find_last_of('.');
    auto clientStyleFileName = fileName.substr(lastOfSlash + 1, lastOfDot - lastOfSlash - 1);
    std::ranges::replace(clientStyleFileName, '<', '.');

    const auto hash = _path.extension().string().substr(0, lastOfDot + 1);

    connection.sendInternal("size:" + std::to_string(fileSize));
    connection.sendInternal("filename:" + clientStyleFileName);
    connection.sendInternal("hash" + hash);

    if (connection.receiveInternal() != "OK") {
        Utils::elog("For some reason remote already has the file, this shouldn't happen");
        connection.receiveInternal();
        return;
    }

    std::ifstream file(_path, std::ios::binary);
    size_t chunkSize = 2 * 1024 * 1024;
    auto buffer = std::make_unique<char[]>(chunkSize);
    size_t sizeRead = 0;
    const auto freeRam = getFreeMemory() / 4;

    while ( true ) {
        file.read(buffer.get(), chunkSize);

        const auto startUploadTime = std::chrono::high_resolution_clock::now();
        connection.send(std::string(buffer.get(), file.gcount()));
        const auto endUploadTime = std::chrono::high_resolution_clock::now();

        std::chrono::duration<double> duration = endUploadTime - startUploadTime;

        sizeRead += file.gcount();

        if ( connection.receiveInternal() != "confirm" )
            throw std::runtime_error("sendFile: client did not confirm the chunk");

        if ( sizeRead == static_cast<unsigned long long>(fileSize) )
            break;

        // adjust chunk size based on duration
        if ( duration.count() > 1.4 ) {
            // decrease chunkSize by 2%, ensure integer rounding
            chunkSize = static_cast<int>(chunkSize * 0.75);

            buffer = std::make_unique<char[]>(chunkSize);
        }
        else if ( duration.count() < 0.3 && freeRam >= chunkSize * 2 ) {
            chunkSize = static_cast<int>(chunkSize * 2);
            buffer = std::make_unique<char[]>(chunkSize);
        }
        else if ( duration.count() < 0.6 && freeRam >= chunkSize * 2 ) {
            chunkSize = static_cast<int>(chunkSize * 1.25);
            buffer = std::make_unique<char[]>(chunkSize);
        }
    }

    connection.sendInternal("DONE");

    if (auto message = connection.receiveInternal();
        message != "OK") {
        Utils::elog(connection.receiveInternal());
    }
}

// set substraction
std::set<std::string> operator/ ( const std::set<std::string>& set, const std::set<std::string>& rhs ) {
    std::set<std::string> result;

    for ( const auto& item: set ) {
        if ( !rhs.contains(item) )
            result.insert(item);
    }

    return result;
}

void ConnectionHandler::_syncAsSlave ( ConnectionServer& connection ) const {
    const std::string user = connection.receiveInternal();
    const std::string pass = connection.receiveInternal();

    if ( !_auth(user, pass) ) {
        connection.sendInternal("NOPE");
        return;
    }

    connection.sendInternal("OK");

    // Master sends array of hashes of his files in format hash|hash|...|
    const auto remoteHashes = _parseHashes<std::set<std::string>>(connection.receiveData());

    // Now we do the same
    const auto localHashes = _getLocalFileHashes<std::set<std::string>>();
    connection.sendData(_generateHashesString(localHashes));

    const auto toGet = remoteHashes / localHashes;
    const auto toSend = localHashes / remoteHashes;

    // Again, master is first to send missing files
    for ( size_t i = 0; i < toGet.size(); i-- ) {
        Utils::log("ConnectionHandler: getting file " + std::to_string(i) + "/" + std::to_string(toGet.size()-1));
        _receiveFile(connection);
    }

    const auto toSendFileNames = _findCorrespondingFileNames<std::set<std::string>>(toSend);

    for ( auto counter = 0; const auto& fileName: toSendFileNames ) {
        Utils::log("ConnectionHandler: sending file " + std::to_string(counter) + "/" + std::to_string(toSend.size()-1));
        _sendFileInSync(connection, fileName);
        ++counter;
    }
}

void ConnectionHandler::_syncer () const {

    while ( !stopRequested ) {

    }
}

template < SetOrVectorOfString T >
T ConnectionHandler::_findCorrespondingFileNames ( const std::set<std::string>& toFind ) const {
    const auto directory = std::filesystem::current_path() / "storage";

    T result;

    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (toFind.contains(entry.path().extension().string().substr(1))) {
            auto tmp = entry.path().filename().string();
        }
    }

    return result;
}

// Parses string in 'hash|hash|...|' format into an array
template < SetOrVectorOfString T >
T ConnectionHandler::_parseHashes ( const std::string& hashesString ) {
    T hashes;

    size_t offset = 0;

    while ( offset < hashesString.length() ) {
        const auto separator = hashesString.find_first_of('|', offset);
        if ( separator == std::string::npos ) { throw std::runtime_error("Parsing hashes: invalid separator"); }

        hashes.emplace(hashesString.substr(offset, separator - offset));
        offset = separator + 1;
    }
    return hashes;
}

template < SetOrVectorOfString T >
T ConnectionHandler::_getLocalFileHashes () {
    T hashes;

    for ( const auto directory = std::filesystem::current_path() / "storage";
          const auto& file: std::filesystem::directory_iterator(directory) ) {
        hashes.emplace(file.path().extension().string().substr(1));
    }

    return hashes;
}

template < SetOrVectorOfString T >
std::string ConnectionHandler::_generateHashesString ( const T& hashes ) {
    std::string result;

    for ( const auto& hash: hashes ) { result += hash + '|'; }

    return result;
}

void ConnectionHandler::_cleanupClientThreads () {
    while ( !stopRequested ) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        std::erase_if(
            clientThreads,
            [] ( const std::jthread& t ) { return t.joinable(); }
        );
    }
}

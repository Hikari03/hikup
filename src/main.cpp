#include <iostream>

#include "CommandType.cpp"
#include "Connection.hpp"
#include "util.cpp"

void sendFile ( std::ifstream& file, const std::ifstream::pos_type fileSize, Connection& connection ) {
    if ( !file.good() ) {
        std::cerr << colorize("Could not open file", Color::RED) << std::endl;
        return;
    }

    const auto freeRam = getFreeMemory() / 4;

    size_t chunkSize = 1024 * 1024;
    auto buffer = std::make_unique<char[]>(chunkSize);

    double readSpeed = 0.0, uploadSpeed = 0.0;

    double totalTimeRead = 0.0, totalTimeUpload = 0.0;

    unsigned long long sizeRead = 0;
    unsigned long long sizeUploaded = 0;

    std::cout << colorize("Starting upload of size: ", Color::BLUE) << colorize(
        humanReadableSize(fileSize), Color::CYAN
    ) << "\n" << std::endl;

    while ( true ) {
        const auto startReadTime = std::chrono::high_resolution_clock::now();
        file.read(buffer.get(), chunkSize);
        const auto endReadTime = std::chrono::high_resolution_clock::now();

        std::chrono::duration<double> duration = endReadTime - startReadTime;

        totalTimeRead += duration.count();
        sizeRead += file.gcount();

        readSpeed = static_cast<double>(sizeRead) / totalTimeRead;

        const auto startUploadTime = std::chrono::high_resolution_clock::now();

        connection.send(std::string(buffer.get(), file.gcount()));

        const auto endUploadTime = std::chrono::high_resolution_clock::now();

        duration = endUploadTime - startUploadTime;

        totalTimeUpload += duration.count();
        sizeUploaded += file.gcount();

        uploadSpeed = static_cast<double>(sizeUploaded) / totalTimeUpload;

        if ( connection.receiveInternal() != "confirm" )
            throw std::runtime_error("Server did not confirm the chunk");

#ifdef HIKUP_DEBUG
        std::cout << "\r" << colorize("Sending data: ", Color::BLUE) +
                colorize(humanReadableSize(sizeUploaded), Color::CYAN) + colorize("/", Color::BLUE) +
                colorize(humanReadableSize(fileSize), Color::CYAN) + colorize(
                    std::string(" (") +
                    std::to_string(( static_cast<double>(sizeUploaded) / static_cast<double>(fileSize) ) * 100.0).
                    substr(0, 5) + " %)",
                    Color::PURPLE
                ) + " ┃ " + colorize("Read: " + humanReadableSpeed(readSpeed), Color::LL_BLUE) + " ━━ "
                + colorize("Up: " + humanReadableSpeed(uploadSpeed), Color::GREEN) + "  " + "| DEBUG | chunk size: " +
                humanReadableSize(chunkSize) << std::flush;
#else
        std::cout << "\r" << colorize("Sending data: ", Color::BLUE) +
                colorize(humanReadableSize(sizeUploaded), Color::CYAN) + colorize("/", Color::BLUE) +
                colorize(humanReadableSize(fileSize), Color::CYAN) + colorize(
                    std::string(" (") +
                    std::to_string(( static_cast<double>(sizeUploaded) / static_cast<double>(fileSize) ) * 100.0).
                    substr(0, 5) + " %)",
                    Color::PURPLE
                ) + " ┃ " + colorize("Read: " + humanReadableSpeed(readSpeed), Color::LL_BLUE) + " ━━ "
                + colorize("Up: " + humanReadableSpeed(uploadSpeed), Color::GREEN) + "  " << std::flush;
#endif

        // this has to be here - if we change the size and break the loop after, wrong amount of data will be sent and probably partly random
        if ( sizeRead == static_cast<unsigned long long>(fileSize) ) {
#ifdef HIKUP_DEBUG
            std::cout << "DEBUG | Final chunk sent, breaking loop. | \nchunkSize: " << chunkSize << "\nsizeRead: " << sizeRead <<
                    "\nsizeUploaded: " << sizeUploaded << "\nfileSize: " << fileSize << std::endl;
#endif
            break;
        }

        // adjust chunk size based on duration
        if (duration.count() > 1.4) {
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

#ifdef HIKUP_DEBUG
    std::cout << "\r" << colorize("Sending data: ", Color::BLUE) +
            colorize(humanReadableSize(sizeUploaded), Color::CYAN) + colorize(
                "/", Color::BLUE
            ) + colorize(
                humanReadableSize(sizeUploaded),
                Color::CYAN
            ) + colorize(" (100 %)  ", Color::PURPLE) + "| DEBUG | chunk size: " + humanReadableSize(chunkSize) <<
            std::endl;
#else
    std::cout << "\r" << colorize("Sending data: ", Color::BLUE) +
            colorize(humanReadableSize(sizeUploaded), Color::CYAN) + colorize(
                "/", Color::BLUE
            ) + colorize(
                humanReadableSize(sizeUploaded),
                Color::CYAN
            ) + colorize(" (100 %)  ", Color::PURPLE) << std::endl;
#endif

    connection.sendInternal("DONE");
    const auto hash = connection.receiveInternal();
    const bool httpExists = std::stoi(connection.receiveInternal());

    std::cout << colorize("File uploaded successfully with hash: ", Color::GREEN) + colorize(hash, Color::CYAN) <<
            std::endl;

    if ( httpExists == true ) {
        connection.sendInternal("getHttpLink");
        const auto httpLink = connection.receiveInternal();
        std::cout << colorize("HTTP link: ", Color::GREEN) + colorize(httpLink, Color::CYAN) << std::endl;
    }
    else { std::cout << colorize("HTTP link: ", Color::GREEN) + colorize("not available", Color::CYAN) << std::endl; }
}

void downloadFile ( Connection& connection ) {
    auto fileSize = std::stoll(connection.receiveInternal());
    auto fileName = connection.receiveInternal();
    double totalTimeDownload = 0.0, totalTimeWrite = 0.0;

    auto freeRam = std::min(getFreeMemory() / 4, static_cast<unsigned long>(fileSize / 16));

    connection.resizeBuffer(freeRam);

    long long sizeWritten = 0;
    unsigned long long sizeDownloaded = 0;


    std::cout << colorize("Downloading file: ", Color::BLUE) + colorize(fileName, Color::CYAN) << colorize(
        " of size: ", Color::BLUE
    ) << colorize(humanReadableSize(fileSize), Color::CYAN) << std::endl;

    // create file
    std::ofstream file(fileName, std::ios::binary);

    while ( true ) {
        auto [chunk,duration] = connection.receiveWTime();

        if ( chunk == _internal"DONE" )
            break;

        sizeDownloaded += chunk.size();
        totalTimeDownload += duration.count();

        auto downloadSpeed = static_cast<double>(sizeDownloaded) / totalTimeDownload;

        auto writeStart = std::chrono::high_resolution_clock::now();
        file.write(chunk.c_str(), static_cast<long>(chunk.size()));
        auto writeEnd = std::chrono::high_resolution_clock::now();

        duration = writeEnd - writeStart;

        sizeWritten += chunk.size();
        totalTimeWrite += duration.count();

        auto writeSpeed = static_cast<double>(sizeWritten) / totalTimeWrite;

        connection.sendInternal("confirm");

        std::cout << "\r" << colorize("Receiving data: ", Color::BLUE) <<
                colorize(humanReadableSize(sizeWritten), Color::CYAN) << colorize("/", Color::BLUE) <<
                colorize(humanReadableSize(fileSize), Color::CYAN) << colorize(
                    std::string(" (") +
                    std::to_string(( static_cast<double>(sizeWritten) / static_cast<double>(fileSize) ) * 100.0).
                    substr(0, 5) + " %)",
                    Color::PURPLE
                ) << " ┃ " << colorize("Write: " + humanReadableSpeed(writeSpeed), Color::LL_BLUE) <<
                " ━━ " + colorize("Down: " + humanReadableSpeed(downloadSpeed), Color::GREEN) << "  " << std::flush;
    }

    std::cout << std::endl;
}

int start ( int argc, char* argv[] ) {
    if ( argc < 4 ) {
        std::cout << "Usage: " << argv[0] << " <up <file> | down <hash> | rm <hash>> <server> \n\n"
                "If file is successfully uploaded, you will get file hash\n"
                "which you need to input if you want to download it\n\n"
                "If server has HTTP server, you will get link for download.\n"
                "You can append '?inplace=yes' to the link to view the file in browser." << std::endl;
        return 1;
    }

    auto command = Command::resolveCommand(argv[1]);

    Connection connection;
    std::ifstream file;
    std::ifstream::pos_type fileSize;
    std::string fileName;
    std::string hash;

    try {
        if ( command == Command::Type::UPLOAD ) {
            auto [_file, _fileSize, _fileName] = resolveFile(argv[2]);
            file = std::move(_file);
            fileSize = _fileSize;
            fileName = _fileName;

            const auto toAllocate = std::min(getFreeMemory() / 4, static_cast<unsigned long>(fileSize / 16));

            std::cout << colorize("Computing hash by chunks of size: ", Color::GREEN) << colorize(
                humanReadableSize(toAllocate), Color::CYAN
            ) << std::endl;
            hash = computeHash(file, toAllocate, fileSize);
            std::cout << colorize("Hash computed", Color::GREEN) << std::endl;
        }

        std::cout << colorize("Connecting to server", Color::GREEN) << std::endl;

        connection.connectToServer(argv[3], 6998);

        std::cout << colorize("Connected to server", Color::GREEN) << std::endl;
    }
    catch ( std::runtime_error& e ) {
        std::cerr << colorize(e.what(), Color::RED) << std::endl;
        return 1;
    }

    connection.sendInternal("command:" + Command::toString(command));
    if ( command == Command::Type::UPLOAD ) {
        connection.sendInternal("size:" + std::to_string(fileSize));
        connection.sendInternal("filename:" + fileName);
        connection.sendInternal("hash:" + hash);
    }
    else if ( command == Command::Type::DOWNLOAD || command == Command::Type::REMOVE ) {
        fileName = argv[2];
        connection.sendInternal("hash:" + fileName);
    }

    if ( connection.receiveInternal() != "OK" ) {
        std::cerr << colorize("Server did not accept the request\n", Color::RED);

        if ( command == Command::Type::UPLOAD ) {
            std::cout << colorize("Reason: file already exists\n", Color::RED) << colorize("Hash: ", Color::PURPLE) <<
                    colorize(hash, Color::CYAN) << std::endl;
            const auto httpLink = connection.receiveInternal();
            std::cout << colorize("HTTP link: ", Color::PURPLE) << colorize(httpLink, Color::CYAN) << std::endl;
        }
        else { std::cout << colorize("Reason: file does not exist", Color::RED) << std::endl; }
        return 1;
    }

    if ( command == Command::Type::REMOVE ) {
        std::cout << colorize("File with hash ", Color::GREEN) << colorize(fileName, Color::CYAN) << colorize(
            " removed!", Color::RED
        ) << std::endl;
        return 0;
    }
    std::cout << colorize("Server ready!\n", Color::GREEN) << std::endl;

    if ( command == Command::Type::UPLOAD )
        sendFile(file, fileSize, connection);
    else if ( command == Command::Type::DOWNLOAD )
        downloadFile(connection);

    return 0;
}

int main ( const int argc, char* argv[] ) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    try { return start(argc, argv); }
    catch ( std::exception& e ) {
        std::cerr << colorize(e.what(), Color::RED) << std::endl;
        return 1;
    }
}

#include "CommandHandlers.hpp"

#include "Color.hpp"
#include "util.cpp"
#include "../shared/FileInfo.hpp"

void CommandHandlers::sendFile ( std::ifstream& file, const std::ifstream::pos_type fileSize, Connection& connection ) {
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
    if ( const auto confirmation = connection.receiveInternal();
        confirmation != "OK") {
        std::cout << colorize("Upload failed with response: " + confirmation, Color::RED);
        return;
    }


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

void CommandHandlers::downloadFile ( Connection& connection ) {
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

int CommandHandlers::listFiles ( Connection& connection, const std::string& user, const std::string& pass ) {
    connection.sendInternal("user:" + user);
    connection.sendInternal("pass:" + pass);

    if ( connection.receiveInternal() != "OK" ) {
        std::cerr << colorize("Authentication failed", Color::RED) << std::endl;
        return 1;
    }

    std::vector<FileInfo> files;
    unsigned maxNameSize = 4, maxSizeSize = 4, maxDateSize = 11; // min sizes for headers

    try {
        std::string fileData;
        while ( ( fileData = connection.receive() ) != _internal"DONE" ) {
            FileInfo fileInfo(fileData.substr(strlen(_data)));
            maxNameSize = std::max(maxNameSize, static_cast<unsigned>(fileInfo.getName().size()));
            maxSizeSize = std::max(maxSizeSize, static_cast<unsigned>(humanReadableSize(fileInfo.getSize()).size()));
            maxDateSize = std::max(maxDateSize, static_cast<unsigned>(fileInfo.getCreationDateString_c().size()));

            files.emplace_back(fileInfo);
        }
    } catch ( std::runtime_error& e ) {
        std::cerr << colorize("Error receiving file list: ", Color::RED) + colorize(e.what(), Color::RED) << std::endl;
        return 1;
    }

    std::cout << padStringToSize("| Name", maxNameSize+2) + " | " +
                padStringToSize("Size", maxSizeSize) + " | " +
                padStringToSize("Upload Date", maxDateSize) + " | " +
                padStringToSize("Hash", files[0].getHash().size()) + "\n";
    std::cout << '|' + std::string(maxNameSize+2, '-') + "|" +
                std::string(maxSizeSize+2, '-') + "|" +
                std::string(maxDateSize+2, '-') + "|" +
                std::string(files[0].getHash().size()+1, '-') + '\n';


    for ( auto& file: files ) {
        std::cout << "| " +
            colorize(
                padStringToSize(
                    file.getName(),
                    maxNameSize),
                Color::CYAN) + " | " +
            colorize(
                padStringToSize(
                    humanReadableSize(
                        file.getSize()),
                        maxSizeSize),
                Color::LL_BLUE) + " | " +
            colorize(file.getCreationDateString_c(), Color::GREEN) + " | " +
            colorize(file.getHash(), Color::PURPLE) + '\n';
    }

    std::cout << '|' + std::string(maxNameSize+2, '-') + "|" +
                std::string(maxSizeSize+2, '-') + "|" +
                std::string(maxDateSize+2, '-') + "|" +
                std::string(files[0].getHash().size()+1, '-') + "\n\n";

    std::cout.flush();

    return 0;
}
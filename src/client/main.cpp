#include <iostream>

#include "BatchHandlers.hpp"
#include "CommandHandlers.hpp"
#include "CommandType.hpp"
#include "../shared/Connection.hpp"

void printHelp ( const std::string& argv0 ) {
    std::cout << "Usage: " << argv0 << " <up <file> | down <hash> | rm <hash> | ls <user> <pass>> <server> \n\n"
                "If file is successfully uploaded, you will get file hash\n"
                "which you need to input if you want to download it.\n\n"
                "For ls command, provide username and password (from server settings).\n\n"
                "If server has HTTP server, you will get link for download.\n"
                "You can append '?view=yes' to the link to view the file in browser.\n\n"
                "You can also replace the file/hash with `-` and pass space/new-line separated list to standard input" << std::endl;
}

int start ( int argc, char* argv[] ) {
    if ( argc < 4 ) {
        printHelp(argv[0]);
        return 1;
    }

    auto command = Command::resolveCommand(argv[1]);

    if ( command == Command::Type::INVALID ) {
        std::cerr << colorize("Invalid command", Color::RED) << std::endl;
        return 1;
    }

    if ( command == Command::Type::LIST && argc != 5 ) {
        std::cerr << colorize("Invalid number of arguments for list command", Color::RED) << std::endl;
        printHelp(argv[0]);
        return 1;
    }

    Connection connection;
    std::ifstream file;
    std::ifstream::pos_type fileSize;
    std::string fileName;
    std::string hash;
    std::string serverAddr;


    if ( (command == Command::Type::UPLOAD ||
         command == Command::Type::REMOVE ||
         command == Command::Type::DOWNLOAD) &&
         !strcmp(argv[2], "-")) {

        command = command + Command::Type::BATCH;

        std::string fileString, tmp;

        while ( !std::cin.eof() ) {
            std::getline(std::cin, tmp);
            fileString += tmp;
        }

        auto files = cutStringIntoVector(fileString);

        connection.connectToServer(argv[3], 6998);

        return Batch::autoResolve(command, connection, files);
    }

    if ( command == Command::Type::LIST ) {
        serverAddr = argv[4];
    }
    else {
        serverAddr = argv[3];
    }

    try {
        if ( command == Command::Type::UPLOAD ) {
            auto [_file, _fileSize, _fileName] = resolveFile(argv[2]);
            file = std::move(_file);
            fileSize = _fileSize;
            fileName = _fileName;


            const auto freeMem = getFreeMemory();
            auto toAllocate = std::min(freeMem / 4, static_cast<unsigned long>(fileSize / 16));
            if ( toAllocate < freeMem / 2 )
                toAllocate = std::min(freeMem, static_cast<unsigned long>(fileSize));


            std::cout << colorize("Computing hash by chunks of size: ", Color::GREEN) << colorize(
                humanReadableSize(toAllocate), Color::CYAN
            ) << std::endl;
            hash = computeHash(file, toAllocate, fileSize);
            std::cout << colorize("Hash computed", Color::GREEN) << std::endl;
        }

        std::cout << colorize("Connecting to server", Color::GREEN) << std::endl;

        connection.connectToServer(serverAddr, 6998);

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

    if ( const auto reason = connection.receiveInternal(); reason != "OK" ) {
        std::cerr << colorize("Server did not accept the request\n", Color::RED);

        if ( command == Command::Type::UPLOAD ) {
            std::cout << colorize("Reason: " + reason + '\n', Color::RED) << colorize("Hash: ", Color::PURPLE) <<
                    colorize(hash, Color::CYAN) << std::endl;
            const auto httpLink = connection.receiveInternal();
            std::cout << colorize("HTTP link: ", Color::PURPLE) << colorize(httpLink, Color::CYAN) << std::endl;
        }

        else { std::cout << colorize("Reason: " + reason, Color::RED) << std::endl; }
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
        CommandHandlers::sendFile(file, fileSize, connection);
    else if ( command == Command::Type::DOWNLOAD )
        CommandHandlers::downloadFile(connection);
    else if ( command == Command::Type::LIST ) {
        return CommandHandlers::listFiles(connection, argv[2], argv[3]);
    }

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

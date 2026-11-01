#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>

#include "BatchHandlers.hpp"
#include "CommandHandlers.hpp"
#include "CommandType.hpp"
#include "E2E.hpp"
#include "Style.hpp"
#include "../shared/Connection.hpp"

using namespace ui;

namespace {

void printHelp ( const std::string& argv0 ) {
    const auto prog = std::filesystem::path(argv0).filename().string();
    auto cmd  = [](const std::string& s) { return paint(s, RUST); };
    auto note = [](const std::string& s) { return paint(s, ASH); };

    std::cout
        << '\n' << paint("::", RUST) << ' ' << paint(prog, BOLD) << note("  // file transfer client") << "\n\n"
        << "  " << note("usage") << "   " << prog << " [q|v]" << cmd("<cmd>") << " <args> <server>[:port]\n\n"
        << "  " << note("commands") << '\n'
        << "    " << cmd("up")   << "  <file>        upload a file, prints its hash\n"
        << "    " << cmd("down") << "  <hash> [dir]  download by hash (dir: directory or file path)\n"
        << "    " << cmd("info") << "  <hash>        show file info (name, size, upload date)\n"
        << "    " << cmd("transfer") << " <url> <server>   the server fetches the URL and stores it\n"
        << "    " << cmd("rm")   << "    <hash>        remove by hash\n"
        << "    " << cmd("ls")   << "    <user> <pass> list files (credentials from server settings)\n\n"
        << "  " << note("modifiers") << " (prefix the command, e.g. qup)\n"
        << "    " << cmd("q") << "   quiet, no decoration, output is only hash/link\n"
        << "    " << cmd("v") << "   force TLS certificate verification\n"
        << "    " << cmd("e") << "   end-to-end encryption (only for up/down)\n"
        << "         " << note("up") << "   encrypts locally, prints the key\n"
        << "         " << note("down") << " decrypts after download, needs " << cmd("<hash>#<key>") << '\n'
        << "         " << note("the server only ever stores ciphertext") << "\n\n"
        << "  " << note("batch") << '\n'
        << "    use " << cmd("-") << " instead of file/hash and pipe a space/newline separated list to stdin\n\n"
        << "  " << note("http") << '\n'
        << "    if the server has an HTTP frontend you get a download link;\n"
        << "    append " << cmd("?view=yes") << " to view the file in a browser\n\n";
    std::cout.flush();
}

struct Endpoint { std::string host; int port = 6998; };

Endpoint parseEndpoint ( std::string s ) {
    Endpoint ep;
    if ( const size_t pos = s.find_last_of(':'); pos != std::string::npos ) {
        try {
            ep.port = std::stoi(s.substr(pos + 1));
            s.resize(pos);
        } catch ( ... ) {}                       // not a port, treat as part of the host
    }
    ep.host = std::move(s);
    return ep;
}

void saveServer ( const std::string& server ) {
    if ( server.empty() ) return;
    try {
        std::filesystem::path targetFile = "/usr/share/hikup";
        if ( std::filesystem::exists(targetFile) && std::filesystem::is_directory(targetFile) ) {
            targetFile /= "servers";
        }

        if ( std::filesystem::exists(targetFile) && std::filesystem::is_regular_file(targetFile) ) {
            std::ifstream in(targetFile);
            std::string line;
            while ( std::getline(in, line) ) {
                auto start = line.find_first_not_of(" \t\r\n");
                if ( start == std::string::npos ) continue;
                auto end = line.find_last_not_of(" \t\r\n");
                line = line.substr(start, end - start + 1);
                if ( line == server ) {
                    return; // already recorded
                }
            }
        }

        if ( targetFile.has_parent_path() ) {
            std::filesystem::create_directories(targetFile.parent_path());
        }
        std::ofstream out(targetFile, std::ios::app);
        if ( out.is_open() ) {
            out << server << '\n';
        }
    } catch ( ... ) {
        // Silently ignore filesystem / permission errors
    }
}

// removes the e2e temporary ciphertext on every exit path (RAII)
struct TempCleaner {
    std::filesystem::path path;
    ~TempCleaner () {
        if ( !path.empty() ) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    }
};

} // namespace

int start ( int argc, char* argv[] ) {
    if ( argc < 4 ) {
        printHelp(argv[0]);
        return 1;
    }

    auto command = Command::resolveCommand(argv[1]);

    const bool SSLVerifyCert = command.contains(Command::Type::SSL_VERIFY_CERT);
    const bool quiet         = command.contains(Command::Type::QUIET);
    const bool e2e           = command.contains(Command::Type::E2E);

    if ( !std::strcmp(argv[2], "-") )
        command.emplace(Command::Type::BATCH);

    if ( !Command::isValid(command) ) {
        fail("invalid command");
        return 1;
    }

    if ( command.contains(Command::Type::INFO) && command.contains(Command::Type::BATCH) ) {
        fail("info does not support batch mode, give a single hash");
        return 1;
    }

    if ( command.contains(Command::Type::TRANSFER) && command.contains(Command::Type::BATCH) ) {
        fail("transfer does not support batch mode, give a single URL");
        return 1;
    }

    if ( e2e && !( command.contains(Command::Type::UPLOAD) || command.contains(Command::Type::DOWNLOAD) ) ) {
        fail("e2e is only supported for up and down");
        return 1;
    }

    if ( e2e && command.contains(Command::Type::BATCH) ) {
        fail("e2e does not support batch mode, give a single file/hash");
        return 1;
    }

    if ( command.contains(Command::Type::LIST) && argc != 5 ) {
        fail("ls needs exactly: ls <user> <pass> <server>[:port]");
        printHelp(argv[0]);
        return 1;
    }

    const std::string rawServer = command.contains(Command::Type::LIST) ? argv[4] : argv[3];
    const auto ep = parseEndpoint(rawServer);

    Connection connection;

    // ── Batch mode: list of files/hashes on stdin ────────────────────────
    if ( ( command.contains(Command::Type::UPLOAD) ||
           command.contains(Command::Type::REMOVE) ||
           command.contains(Command::Type::DOWNLOAD) ) &&
         command.contains(Command::Type::BATCH) ) {

        std::string input{ std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>() };
        std::ranges::replace_if(input, [](char c) { return c == '\n' || c == '\r'; }, ' ');
        const auto files = cutStringIntoVector(input);

        if ( files.empty() ) {
            fail("nothing to do, stdin was empty");
            return 1;
        }

        try {
            connection.connectToServer(ep.host, ep.port, SSLVerifyCert);
            saveServer(rawServer);
        } catch ( std::runtime_error& e ) {
            fail(e.what());
            return 1;
        }

        return Batch::autoResolve(command, connection, files, quiet);
    }

    // ── Single command ───────────────────────────────────────────────────
    std::ifstream file;
    std::ifstream::pos_type fileSize{};
    std::string fileName, hash;
    std::filesystem::path e2eTempFile;
    std::string e2eKey;
    TempCleaner e2eCleanup;   // removes the ciphertext temp file on any exit

    try {
        if ( command.contains(Command::Type::UPLOAD) ) {
            const std::filesystem::path sourcePath = std::filesystem::absolute(argv[2]);

            if ( e2e ) {
                if ( !quiet ) step("encrypting", sourcePath.filename().string());
                auto [temp, key] = E2E::encryptFile(sourcePath, quiet);
                e2eTempFile    = temp;
                e2eCleanup.path = temp;
                e2eKey          = key;
                fileName    = sourcePath.filename().string();   // keep the original name
                fileSize    = std::filesystem::file_size(e2eTempFile);
                file.open(e2eTempFile, std::ios::binary);
                if ( !file ) throw std::runtime_error("could not reopen the encrypted file");
                if ( !quiet ) done("encrypted", "with a fresh key");
            }
            else {
                auto [_file, _fileSize, _fileName] = resolveFile(argv[2]);
                file     = std::move(_file);
                fileSize = _fileSize;
                fileName = _fileName;
            }

            if ( !quiet ) step("hashing", fileName);
            hash = computeHash(file, fileSize, quiet);
            if ( !quiet ) done("hashed");
        }

        if ( !quiet ) step("connecting", ep.host + ":" + std::to_string(ep.port));
        connection.connectToServer(ep.host, ep.port, SSLVerifyCert);
        saveServer(rawServer);
        if ( !quiet ) done("connected");
    }
    catch ( std::runtime_error& e ) {
        fail(e.what());
        return 1;
    }

    // ── Handshake ────────────────────────────────────────────────────────
    connection.sendInternal("command:" + Command::toString(Command::selectBasic(command)));
    if ( command.contains(Command::Type::UPLOAD) ) {
        connection.sendInternal("size:" + std::to_string(fileSize));
        connection.sendInternal("filename:" + fileName);
        connection.sendInternal("hash:" + hash);
    }
    else if ( command.contains(Command::Type::DOWNLOAD) || command.contains(Command::Type::REMOVE)
              || command.contains(Command::Type::INFO) ) {
        fileName = argv[2];                                  // here: the hash

        if ( e2e && command.contains(Command::Type::DOWNLOAD) ) {
            // the e2e key rides along with the hash: <hash>#<key>
            if ( const size_t pos = fileName.find('#'); pos != std::string::npos ) {
                e2eKey = fileName.substr(pos + 1);
                fileName.resize(pos);
            }
            if ( e2eKey.empty() ) {
                fail("e2e download needs the key: use <hash>#<key> (as printed by the uploader)");
                return 1;
            }
        }

        connection.sendInternal("hash:" + fileName);
    }
    else if ( command.contains(Command::Type::TRANSFER) ) {
        connection.sendInternal("url:" + std::string(argv[2]));
    }

    if ( const auto reason = connection.receiveInternal(); reason != "OK" ) {
        if ( command.contains(Command::Type::UPLOAD) ) {
            // server already has this file and answers with its link
            const auto httpLink = connection.receiveInternal();
            if ( quiet ) {
                std::cout << httpLink << '\n';
            } else {
                std::cerr << tag("!", BRICK) << " server refused the upload: " << reason << '\n';
                printTree(paint("    already known to the server", ASH), {
                    { "hash", hash,     RUST  },
                    { "link", httpLink, AMBER },
                }, std::cout);
            }
        } else {
            fail("server refused the request: " + reason);
        }
        return 1;
    }

    if ( command.contains(Command::Type::REMOVE) ) {
        if ( !quiet ) done("removed", fileName);
        return 0;
    }

    // ── Dispatch ─────────────────────────────────────────────────────────
    if ( command.contains(Command::Type::UPLOAD) )
        CommandHandlers::sendFile(file, fileSize, connection, quiet, e2eKey);
    else if ( command.contains(Command::Type::DOWNLOAD) ) {
        if ( e2e )
            return CommandHandlers::downloadFileE2E(connection, e2eKey, quiet);
        const std::filesystem::path destination = argc > 4
            ? std::filesystem::path(argv[4])
            : std::filesystem::path{};
        CommandHandlers::downloadFile(connection, quiet, destination);
    }
    else if ( command.contains(Command::Type::INFO) )
        return CommandHandlers::infoFile(connection, quiet);
    else if ( command.contains(Command::Type::TRANSFER) )
        return CommandHandlers::transferUrl(connection, quiet);
    else if ( command.contains(Command::Type::LIST) )
        return CommandHandlers::listFiles(connection, argv[2], argv[3], quiet);

    return 0;
}

int main ( const int argc, char* argv[] ) {

    try { return start(argc, argv); }
    catch ( std::exception& e ) {
        fail(e.what());
        return 1;
    }
}

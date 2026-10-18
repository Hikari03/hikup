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
        << "    " << cmd("down") << "  <hash>        download by hash\n"
        << "    " << cmd("rm")   << "    <hash>        remove by hash\n"
        << "    " << cmd("ls")   << "    <user> <pass> list files (credentials from server settings)\n\n"
        << "  " << note("modifiers") << " (prefix the command, e.g. qup)\n"
        << "    " << cmd("q") << "   quiet, no decoration, output is only hash/link\n"
        << "    " << cmd("v") << "   force TLS certificate verification\n\n"
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

} // namespace

int start ( int argc, char* argv[] ) {
    if ( argc < 4 ) {
        printHelp(argv[0]);
        return 1;
    }

    auto command = Command::resolveCommand(argv[1]);

    const bool SSLVerifyCert = command.contains(Command::Type::SSL_VERIFY_CERT);
    const bool quiet         = command.contains(Command::Type::QUIET);

    if ( !std::strcmp(argv[2], "-") )
        command.emplace(Command::Type::BATCH);

    if ( !Command::isValid(command) ) {
        fail("invalid command");
        return 1;
    }

    if ( command.contains(Command::Type::LIST) && argc != 5 ) {
        fail("ls needs exactly: ls <user> <pass> <server>[:port]");
        printHelp(argv[0]);
        return 1;
    }

    const auto ep = parseEndpoint(command.contains(Command::Type::LIST) ? argv[4] : argv[3]);

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

    try {
        if ( command.contains(Command::Type::UPLOAD) ) {
            auto [_file, _fileSize, _fileName] = resolveFile(argv[2]);
            file     = std::move(_file);
            fileSize = _fileSize;
            fileName = _fileName;

            if ( !quiet ) step("hashing", fileName);
            hash = computeHash(file, fileSize, quiet);
            if ( !quiet ) done("hashed");
        }

        if ( !quiet ) step("connecting", ep.host + ":" + std::to_string(ep.port));
        connection.connectToServer(ep.host, ep.port, SSLVerifyCert);
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
    else if ( command.contains(Command::Type::DOWNLOAD) || command.contains(Command::Type::REMOVE) ) {
        fileName = argv[2];                                  // here: the hash
        connection.sendInternal("hash:" + fileName);
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
        CommandHandlers::sendFile(file, fileSize, connection, quiet);
    else if ( command.contains(Command::Type::DOWNLOAD) )
        CommandHandlers::downloadFile(connection, quiet);
    else if ( command.contains(Command::Type::LIST) )
        return CommandHandlers::listFiles(connection, argv[2], argv[3]);

    return 0;
}

int main ( const int argc, char* argv[] ) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    try { return start(argc, argv); }
    catch ( std::exception& e ) {
        fail(e.what());
        return 1;
    }
}
#include "CommandHandlers.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/ioctl.h>   // Windows: GetConsoleScreenBufferInfo
#include <unistd.h>      // isatty (Windows: <io.h>, _isatty/_fileno)
#include <vector>

#include "Color.hpp"     // drop if nothing else in this file uses colorize()
#include "E2E.hpp"
#include "util.cpp"
#include "Style.hpp"
#include "../shared/FileInfo.hpp"

namespace ui {
    void drawProgress(const std::string& verb, unsigned long long done, unsigned long long total,
                  double speed, size_t termW) {
        drawBar(verb,
                total ? double(done) / double(total) : 1.0,
                humanReadableSize(done) + "/" + humanReadableSize(total),
                humanReadableSpeed(speed),
                speed > 0 ? "eta " + formatDuration(double(total - done) / speed) : "eta --",
                termW);
    }
}


// ═════════════════════════════════════════════════════════════════════════
void CommandHandlers::sendFile ( std::ifstream& file, const std::ifstream::pos_type fileSize, Connection& connection, const bool quiet, const std::string& e2eKey ) {
    if ( !file.good() ) { ui::fail("could not open file"); return; }

    using clock = std::chrono::steady_clock;
    const auto total = static_cast<unsigned long long>(fileSize);

    const unsigned long freeRam = std::clamp(static_cast<unsigned long>(getFreeMemory() / 4),
                                             static_cast<unsigned long>(8 * 1024 * 1024),
                                             static_cast<unsigned long>(64 * 1024 * 1024));

    size_t chunkSize = 8 * 1024 * 1024;
    std::vector<char> buffer(chunkSize);

    double upTime = 0.0;
    unsigned long long sent = 0;

    const bool showProgress = !quiet && ui::g_tty;
    const size_t termW = ui::terminalWidth();

    const auto start = clock::now();
    auto lastDraw = start - std::chrono::seconds(1);

    while ( sent < total ) {
        file.read(buffer.data(), static_cast<std::streamsize>(chunkSize));
        const auto got = static_cast<size_t>(file.gcount());
        if ( got == 0 ) break;                          // truncated file / read error

        const auto t0 = clock::now();
        connection.sendRaw(buffer.data(), got);
        const std::chrono::duration<double> upDur = clock::now() - t0;

        upTime += upDur.count();
        sent   += got;

        if ( showProgress && clock::now() - lastDraw >= std::chrono::milliseconds(80) ) {
            ui::drawProgress("uploading", sent, total, upTime > 0 ? static_cast<double>(sent) / upTime : 0.0, termW);
            lastDraw = clock::now();
        }

        size_t newSize = chunkSize;
        if ( upDur.count() > 1.0 )                                  newSize = std::max<size_t>(1024 * 1024, chunkSize * 0.75);
        else if ( upDur.count() < 0.2 && freeRam >= chunkSize * 2 ) newSize = chunkSize * 2;
        else if ( upDur.count() < 0.4 && freeRam >= chunkSize * 2 ) newSize = chunkSize * 1.25;

        if ( newSize != chunkSize ) { chunkSize = newSize; buffer.resize(chunkSize); }
    }

    const std::chrono::duration<double> wall = clock::now() - start;
    if ( showProgress ) ui::clearLine();

    if ( sent != total ) {
        ui::fail("upload incomplete: sent " + humanReadableSize(sent) + " of " + humanReadableSize(total));
        return;
    }

    if ( const auto confirmation = connection.receiveInternal(); confirmation != "OK" ) {
        ui::fail("upload failed: " + confirmation);
        return;
    }

    const auto hash = connection.receiveInternal();
    const bool httpExists = std::stoi(connection.receiveInternal()) != 0;

    std::string httpLink;
    if ( httpExists ) {
        connection.sendInternal("getHttpLink");
        httpLink = connection.receiveInternal();
    }

    if ( quiet ) {
        std::cout << "hash: " << hash << '\n';
        if ( !e2eKey.empty() ) std::cout << "key: " << e2eKey << '\n';
        if ( httpExists ) std::cout << "http: " << httpLink << "\n\n";
        return;
    }

    const double avg = wall.count() > 0 ? double(total) / wall.count() : 0.0;
    std::vector<ui::Field> fields = {
        { "time", ui::formatDuration(wall.count()) + " @ " + humanReadableSpeed(avg), ui::SAND },
        { "hash", hash, ui::RUST },
    };
    if ( !e2eKey.empty() )
        fields.push_back({ "key", e2eKey, ui::AMBER });
    fields.push_back({ "link", httpExists ? httpLink : "not available",                       httpExists ? ui::AMBER : ui::ASH });
#ifdef HIKUP_DEBUG
    fields.push_back({ "chunk", humanReadableSize(chunkSize), ui::ASH });
#endif
    printTree(ui::tag("+", ui::MOSS) + " " + ui::paint("uploaded", ui::BOLD) + " " + ui::paint(humanReadableSize(fileSize), ui::AMBER), fields);
}

// ═════════════════════════════════════════════════════════════════════════
std::string CommandHandlers::downloadFile ( Connection& connection, const bool quiet,
                                             const std::filesystem::path& destination, const bool summary ) {
    using clock = std::chrono::steady_clock;

    const auto fileSize = std::stoll(connection.receiveInternal());
    const auto total    = static_cast<unsigned long long>(fileSize);

    // never trust a server-supplied name as a path
    const std::string fileName = std::filesystem::path(connection.receiveInternal()).filename().string();
    if ( fileName.empty() || fileName == "." || fileName == ".." ) {
        ui::fail("server sent an invalid file name");
        return {};
    }

    const std::filesystem::path outputPath = destination.empty()
                                                 ? std::filesystem::path(fileName)
                                                 : destination;

    const unsigned long freeRam = std::clamp(static_cast<unsigned long>(getFreeMemory() / 4),
                                             static_cast<unsigned long>(4 * 1024 * 1024),
                                             static_cast<unsigned long>(64 * 1024 * 1024));
    connection.resizeBuffer(freeRam);

    std::ofstream file(outputPath, std::ios::binary);
    if ( !file ) { ui::fail("could not create " + outputPath.string()); return {}; }

    const bool showProgress = !quiet && ui::g_tty;
    const size_t termW = ui::terminalWidth();
    unsigned long long got = 0;

    if ( !quiet ) {
        std::cout << '\n' << ui::tag("*", ui::RUST) << ' ' << ui::paint("downloading", ui::BOLD) << ' '
                  << ui::styledName(fileName) << '\n';
    }

    const auto start = clock::now();
    auto lastDraw = start - std::chrono::seconds(1);

    try {
        connection.receiveExact(fileSize, [&](const char* chunk, size_t size) {
            file.write(chunk, static_cast<std::streamsize>(size));
            if ( !file ) throw std::runtime_error("failed to write to disk");
            got += size;

            if ( showProgress && clock::now() - lastDraw >= std::chrono::milliseconds(80) ) {
                const std::chrono::duration<double> el = clock::now() - start;
                ui::drawProgress("downloading", got, total, el.count() > 0 ? static_cast<double>(got) / el.count() : 0.0, termW);
                lastDraw = clock::now();
            }
        });
    } catch ( const std::exception& e ) {
        if ( showProgress )
            ui::clearLine();
        ui::fail(std::string("download failed: ") + e.what());
        file.close();
        std::error_code ec;
        std::filesystem::remove(outputPath, ec);         // no partial files
        return {};
    }

    const std::chrono::duration<double> wall = clock::now() - start;
    file.close();
    if ( showProgress )
        ui::clearLine();

    connection.sendInternal("OK");

    if ( quiet || !summary )
        return fileName;

    const double avg = wall.count() > 0 ? double(total) / wall.count() : 0.0;
    ui::printTree(ui::tag("+", ui::MOSS) + " " + ui::paint("downloaded", ui::BOLD) + " " + ui::styledName(fileName), {
                      { "size", humanReadableSize(fileSize), ui::AMBER },
                      { "time", ui::formatDuration(wall.count()) + " @ " + humanReadableSpeed(avg), ui::SAND  },
                  });
    return fileName;
}

// ═════════════════════════════════════════════════════════════════════════
int CommandHandlers::downloadFileE2E ( Connection& connection, const std::string& key, const bool quiet ) {
    // download the ciphertext into a temporary file, then decrypt in place
    const auto temp = std::filesystem::temp_directory_path()
                   / ( "hikup-e2e-" + std::to_string(::getpid()) + "-download.tmp" );

    const std::string name = downloadFile(connection, quiet, temp, false);

    std::error_code ec;
    if ( name.empty() ) {
        std::filesystem::remove(temp, ec);
        return 1;
    }

    try {
        E2E::decryptFile(temp, key, name, quiet);
    } catch ( const std::exception& e ) {
        ui::fail(e.what());
        std::filesystem::remove(temp, ec);
        std::filesystem::remove(name, ec);               // no partial plaintext
        return 1;
    }

    std::filesystem::remove(temp, ec);

    if ( !quiet ) {
        ui::printTree(ui::tag("+", ui::MOSS) + " " + ui::paint("downloaded", ui::BOLD) + " " + ui::styledName(name) + ' '
                         + ui::paint("(decrypted)", ui::ASH), {
                          { "size", humanReadableSize(std::filesystem::file_size(name)), ui::AMBER },
                      });
    }
    return 0;
}

// ═════════════════════════════════════════════════════════════════════════
int CommandHandlers::infoFile ( Connection& connection, const bool quiet ) {
    std::string encoded;
    try {
        encoded = connection.receiveData();
    } catch ( const std::exception& e ) {
        ui::fail(std::string("error receiving file info: ") + e.what());
        return 1;
    }

    FileInfo file(encoded);

    if ( quiet ) {
        const auto uploaded = std::chrono::duration_cast<std::chrono::seconds>(
                                  file.getCreationDateUTC().time_since_epoch()).count();
        std::cout << file.getName() << ' ' << file.getSize() << ' ' << uploaded << '\n';
        return 0;
    }

    ui::printTree(ui::tag("*", ui::RUST) + ' ' + ui::paint("info", ui::BOLD) + ' ' + ui::styledName(file.getName()), {
                      { "size",     humanReadableSize(file.getSize()), ui::AMBER },
                      { "uploaded", file.getCreationDateString_c(),    ui::SAND  },
                      { "hash",     file.getHash(),                    ui::RUST  },
                  });
    return 0;
}

// ═════════════════════════════════════════════════════════════════════════
int CommandHandlers::listFiles ( Connection& connection, const std::string& user, const std::string& pass, const bool quiet ) {
    connection.sendInternal("user:" + user);
    connection.sendInternal("pass:" + pass);

    if ( connection.receiveInternal() != "OK" ) {
        ui::fail("authentication failed");
        return 1;
    }

    std::vector<FileInfo> files;
    try {
        std::string fileData;
        while ( ( fileData = connection.receive() ) != _internal"DONE" ) {
            files.emplace_back(fileData.substr(strlen(_data)));
        }
    } catch ( std::runtime_error& e ) {
        ui::fail(std::string("error receiving file list: ") + e.what());
        return 1;
    }

    if ( quiet ) {
        // machine-readable output for scripts (and the GNOME extension):
        // one line per file: name \t size in bytes \t upload date (epoch seconds) \t hash
        for ( const auto& file: files ) {
            const auto uploaded = std::chrono::duration_cast<std::chrono::seconds>(
                                      file.getCreationDateUTC().time_since_epoch()).count();
            std::cout << file.getName() << '\t' << file.getSize() << '\t' << uploaded << '\t'
                      << file.getHash() << '\n';
        }
        return 0;
    }

    if ( files.empty() ) {
        std::cout << ui::tag("-", ui::ASH) << ' ' << ui::paint("no files found", ui::ASH) << '\n';
        return 0;
    }

    // ── Measure ──────────────────────────────────────────────────────────
    struct Row { std::string name, size, date, hash; const char* sizeCol; };
    size_t wName = 4, wSize = 4, wDate = 8, wHash = 4;
    std::vector<Row> rows;
    rows.reserve(files.size());

    decltype(files[0].getSize()) total{};
    for ( auto& f : files ) {
        Row r{ f.getName(), humanReadableSize(f.getSize()), f.getCreationDateString_c(),
               f.getHash(),
               ui::sizeColor(static_cast<double>(f.getSize())) };
        wName = std::max(wName, ui::displayWidth(r.name));
        wSize = std::max(wSize, ui::displayWidth(r.size));
        wDate = std::max(wDate, ui::displayWidth(r.date));
        wHash = std::max(wHash, ui::displayWidth(r.hash));
        total += f.getSize();
        rows.push_back(std::move(r));
    }

    // ── Layout: hash inline if it fits, otherwise on its own └─ line ─────
    const size_t gapW = 2, indent = 2;
    const size_t termW = ui::terminalWidth();
    const size_t inlineW = indent + wName + wSize + wDate + wHash + 3 * gapW;
    const bool inlineHash = termW == 0 || inlineW <= termW;
    const std::string gap(gapW, ' '), pad(indent, ' ');

    // ── Header ───────────────────────────────────────────────────────────
    //  :: files (3)  // 813.2 MB total
    std::cout << '\n' << ui::paint("::", ui::RUST) << ' ' << ui::paint("files", ui::BOLD)
              << ui::paint(" (" + std::to_string(files.size()) + ")  // " + humanReadableSize(total) + " total", ui::ASH)
              << "\n\n";

    std::string head = pad + ui::paint("name", ui::ASH) + ui::spaces(wName - 4) + gap
                     + ui::spaces(wSize - 4) + ui::paint("size", ui::ASH) + gap
                     + ui::paint("uploaded", ui::ASH) + ui::spaces(wDate - 8);
    std::string rule = pad + ui::paint(ui::repeat("─", wName), ui::ASH) + gap
                     + ui::paint(ui::repeat("─", wSize), ui::ASH) + gap
                     + ui::paint(ui::repeat("─", wDate), ui::ASH);
    if ( inlineHash ) {
        head += gap + ui::paint("hash", ui::ASH);
        rule += gap + ui::paint(ui::repeat("─", wHash), ui::ASH);
    }
    std::cout << head << '\n' << rule << '\n';

    // ── Rows ─────────────────────────────────────────────────────────────
    for ( const auto& r : rows ) {
        std::cout << pad << ui::styledName(r.name) << ui::spaces(wName - ui::displayWidth(r.name)) << gap
                  << ui::spaces(wSize - ui::displayWidth(r.size)) << ui::paint(r.size, r.sizeCol) << gap
                  << ui::paint(r.date, ui::ASH);
        if ( inlineHash ) {
            std::cout << ui::spaces(wDate - ui::displayWidth(r.date)) << gap << ui::paint(r.hash, ui::RUST) << '\n';
        } else {
            std::cout << '\n' << pad << ui::paint("└─ ", ui::ASH) << ui::paint(r.hash, ui::RUST) << '\n';
        }
    }

    std::cout << std::endl;
    return 0;
}
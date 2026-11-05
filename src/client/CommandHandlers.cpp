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
CommandHandlers::UploadResult CommandHandlers::sendFile ( std::ifstream& file, const std::ifstream::pos_type fileSize,
                                                          Connection& connection, const bool quiet,
                                                          const std::string& e2eKey, const ui::ProgressFn& progress ) {
    UploadResult result;
    if ( !file.good() ) {
        result.error = "could not open file";
        if ( !progress ) ui::fail(result.error);
        return result;
    }

    using clock = std::chrono::steady_clock;
    const auto total = static_cast<unsigned long long>(fileSize);

    const unsigned long freeRam = std::clamp(static_cast<unsigned long>(getFreeMemory() / 4),
                                             static_cast<unsigned long>(8 * 1024 * 1024),
                                             static_cast<unsigned long>(64 * 1024 * 1024));

    double upTime = 0.0;
    unsigned long long sent = 0;

    // machine-readable progress for wrappers (e.g. the GNOME extension):
    // "PROGRESS <sent> <total>" lines on stdout, throttled to 50 ms
    const bool progressEnv = std::getenv("HIKUP_PROGRESS") != nullptr;
    auto lastProgress = clock::now() - std::chrono::seconds(1);

    if ( progressEnv )
        std::cout << "PROGRESS 0 " << total << '\n' << std::flush;

    // start small so progress lands early and often; AdaptiveBuffer ramps the
    // chunk back up on fast links (256 KB → 64 MB takes ~8 doublings)
    AdaptiveBuffer buffer;

    const bool showProgress = !quiet && ui::g_tty;
    const size_t termW = ui::terminalWidth();

    const auto start = clock::now();
    auto lastDraw = start - std::chrono::seconds(1);

    while ( sent < total ) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto got = static_cast<size_t>(file.gcount());
        if ( got == 0 ) break;                          // truncated file / read error

        const auto t0 = clock::now();
        connection.sendRaw(buffer.data(), got);
        const std::chrono::duration<double> upDur = clock::now() - t0;

        upTime += upDur.count();
        sent   += got;

        if ( progressEnv && clock::now() - lastProgress >= std::chrono::milliseconds(50) ) {
            std::cout << "PROGRESS " << sent << ' ' << total << '\n' << std::flush;
            lastProgress = clock::now();
        }

        if ( clock::now() - lastDraw >= std::chrono::milliseconds(80) ) {
            if ( progress )
                // a batch progress block renders the row instead
                progress(sent, total, upTime > 0 ? static_cast<double>(sent) / upTime : 0.0);
            else if ( showProgress )
                ui::drawProgress("uploading", sent, total, upTime > 0 ? static_cast<double>(sent) / upTime : 0.0, termW);
            lastDraw = clock::now();
        }

        buffer.reportSendDuration(upDur.count(), freeRam);
    }

    if ( progressEnv )
        std::cout << "PROGRESS " << sent << ' ' << total << '\n' << std::flush;

    const std::chrono::duration<double> wall = clock::now() - start;
    if ( showProgress && !progress ) ui::clearLine();

    if ( sent != total ) {
        result.error = "upload incomplete: sent " + humanReadableSize(sent) + " of " + humanReadableSize(total);
        if ( !progress ) ui::fail(result.error);
        return result;
    }

    if ( const auto confirmation = connection.receiveInternal(); confirmation != "OK" ) {
        result.error = "upload failed: " + confirmation;
        if ( !progress ) ui::fail(result.error);
        return result;
    }

    const auto hash = connection.receiveInternal();
    const bool httpExists = std::stoi(connection.receiveInternal()) != 0;

    std::string httpLink;
    if ( httpExists ) {
        connection.sendInternal("getHttpLink");
        httpLink = connection.receiveInternal();
    }

    result.ok = true;
    result.seconds = wall.count();
    result.speed = wall.count() > 0 ? double(total) / wall.count() : 0.0;
    result.hash = hash;
    result.httpLink = httpLink;

    if ( quiet ) {
        std::cout << "hash: " << hash << '\n';
        if ( !e2eKey.empty() ) std::cout << "key: " << e2eKey << '\n';
        if ( httpExists ) std::cout << "http: " << httpLink << "\n\n";
        return result;
    }

    // a batch progress block is drawing this upload — the caller renders
    // the result so nothing prints below the block mid-flight
    if ( progress )
        return result;

    const double avg = result.speed;
    std::vector<ui::Field> fields = {
        { "time", ui::formatDuration(wall.count()) + " @ " + humanReadableSpeed(avg), ui::ICE_WHITE },
        { "hash", hash, ui::AERO_SKY },
    };
    if ( !e2eKey.empty() )
        fields.push_back({ "key", e2eKey, ui::LUMINOUS_GLASS });
    fields.push_back({ "link", httpExists ? httpLink : "not available",                       httpExists ? ui::LUMINOUS_GLASS : ui::FROSTED_STEEL });
#ifdef HIKUP_DEBUG
    fields.push_back({ "chunk", humanReadableSize(buffer.size()), ui::FROSTED_STEEL });
#endif
    printTree(ui::tag("+", ui::AQUA_GLASS) + " " + ui::paint("uploaded", ui::BOLD) + " " + ui::paint(humanReadableSize(fileSize), ui::LUMINOUS_GLASS), fields);
    return result;
}

// ═════════════════════════════════════════════════════════════════════════
std::string CommandHandlers::downloadFile ( Connection& connection, const bool quiet,
                                             const std::filesystem::path& destination, const bool summary,
                                             const ui::ProgressFn& progress ) {
    using clock = std::chrono::steady_clock;

    const auto fileSize = std::stoll(connection.receiveInternal());
    const auto total    = static_cast<unsigned long long>(fileSize);

    // never trust a server-supplied name as a path
    const std::string fileName = std::filesystem::path(connection.receiveInternal()).filename().string();
    if ( fileName.empty() || fileName == "." || fileName == ".." ) {
        if ( !progress ) ui::fail("server sent an invalid file name");
        return {};
    }

    // tell the server we consumed all header messages and are ready for the
    // raw stream — without this, raw data can coalesce into the header reads
    // and the client waits forever for a message end-marker (race)
    connection.sendInternal("OK");

    std::filesystem::path outputPath;
    if ( destination.empty() )
        outputPath = fileName;                        // current directory
    else if ( std::filesystem::is_directory(destination) )
        outputPath = destination / fileName;          // a directory: keep the server-side name
    else
        outputPath = destination;                     // an explicit file path

    const unsigned long freeRam = std::clamp(static_cast<unsigned long>(getFreeMemory() / 4),
                                             static_cast<unsigned long>(4 * 1024 * 1024),
                                             static_cast<unsigned long>(64 * 1024 * 1024));
    connection.resizeBuffer(freeRam);

    std::ofstream file(outputPath, std::ios::binary);
    if ( !file ) {
        if ( !progress ) ui::fail("could not create " + outputPath.string());
        return {};
    }

    const bool showProgress = !quiet && ui::g_tty;
    const size_t termW = ui::terminalWidth();
    unsigned long long got = 0;

    // a batch progress block shows this file as one of its rows
    if ( !quiet && !progress ) {
        std::cout << '\n' << ui::tag("*", ui::AERO_SKY) << ' ' << ui::paint("downloading", ui::BOLD) << ' '
                  << ui::styledName(fileName) << '\n';
    }

    const auto start = clock::now();
    auto lastDraw = start - std::chrono::seconds(1);

    // machine-readable progress for wrappers (e.g. the GNOME extension):
    // "PROGRESS <got> <total>" lines on stdout, throttled to 50 ms
    const bool progressEnv = std::getenv("HIKUP_PROGRESS") != nullptr;
    auto lastProgress = clock::now() - std::chrono::seconds(1);

    if ( progressEnv )
        std::cout << "PROGRESS 0 " << total << '\n' << std::flush;

    try {
        connection.receiveExact(fileSize, [&](const char* chunk, size_t size) {
            file.write(chunk, static_cast<std::streamsize>(size));
            if ( !file ) throw std::runtime_error("failed to write to disk");
            got += size;

            if ( progressEnv && clock::now() - lastProgress >= std::chrono::milliseconds(50) ) {
                std::cout << "PROGRESS " << got << ' ' << total << '\n' << std::flush;
                lastProgress = clock::now();
            }

            if ( clock::now() - lastDraw >= std::chrono::milliseconds(80) ) {
                if ( progress ) {
                    // a batch progress block renders the row instead
                    const std::chrono::duration<double> el = clock::now() - start;
                    progress(got, total, el.count() > 0 ? static_cast<double>(got) / el.count() : 0.0);
                } else if ( showProgress ) {
                    const std::chrono::duration<double> el = clock::now() - start;
                    ui::drawProgress("downloading", got, total, el.count() > 0 ? static_cast<double>(got) / el.count() : 0.0, termW);
                }
                lastDraw = clock::now();
            }
        });
    } catch ( const std::exception& e ) {
        if ( showProgress && !progress )
            ui::clearLine();
        if ( !progress ) ui::fail(std::string("download failed: ") + e.what());
        file.close();
        std::error_code ec;
        std::filesystem::remove(outputPath, ec);         // no partial files
        return {};
    }

    if ( progressEnv )
        std::cout << "PROGRESS " << got << ' ' << total << '\n' << std::flush;

    const std::chrono::duration<double> wall = clock::now() - start;
    file.close();
    if ( showProgress && !progress )
        ui::clearLine();

    connection.sendInternal("OK");

    if ( quiet || !summary )
        return fileName;

    const double avg = wall.count() > 0 ? double(total) / wall.count() : 0.0;
    ui::printTree(ui::tag("+", ui::AQUA_GLASS) + " " + ui::paint("downloaded", ui::BOLD) + " " + ui::styledName(fileName), {
                      { "size", humanReadableSize(fileSize), ui::LUMINOUS_GLASS },
                      { "time", ui::formatDuration(wall.count()) + " @ " + humanReadableSpeed(avg), ui::ICE_WHITE  },
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
        ui::printTree(ui::tag("+", ui::AQUA_GLASS) + " " + ui::paint("downloaded", ui::BOLD) + " " + ui::styledName(name) + ' '
                         + ui::paint("(decrypted)", ui::FROSTED_STEEL), {
                          { "size", humanReadableSize(std::filesystem::file_size(name)), ui::LUMINOUS_GLASS },
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

    ui::printTree(ui::tag("*", ui::AERO_SKY) + ' ' + ui::paint("info", ui::BOLD) + ' ' + ui::styledName(file.getName()), {
                      { "size",     humanReadableSize(file.getSize()), ui::LUMINOUS_GLASS },
                      { "uploaded", file.getCreationDateString_c(),    ui::ICE_WHITE  },
                      { "hash",     file.getHash(),                    ui::AERO_SKY  },
                  });
    return 0;
}

// ═════════════════════════════════════════════════════════════════════════
int CommandHandlers::fetchUrl ( Connection& connection, const bool quiet ) {
    using clock = std::chrono::steady_clock;

    // live progress while the server fetches: "PROGRESS <now> <total>" lines
    // arrive until the final result message
    const bool showProgress = !quiet && ui::g_tty;
    const size_t termW = ui::terminalWidth();
    const auto startT = clock::now();
    auto lastDraw = startT - std::chrono::seconds(1);

    std::string line;
    while ( ( line = connection.receiveInternal() ).starts_with("PROGRESS ") ) {
        if ( !showProgress || clock::now() - lastDraw < std::chrono::milliseconds(100) )
            continue;   // quiet mode still consumes the lines (protocol sync)

        const auto parts = line.substr(strlen("PROGRESS "));
        const auto split = parts.find(' ');
        const unsigned long long now = split == std::string::npos ? 0 : std::stoull(parts.substr(0, split));
        const unsigned long long total = split == std::string::npos ? 0 : std::stoull(parts.substr(split + 1));

        if ( total > 0 ) {
            const std::chrono::duration<double> el = clock::now() - startT;
            ui::drawProgress("fetching", now, total,
                              el.count() > 0 ? double(now) / el.count() : 0.0, termW);
        } else {
            // unknown size (no Content-Length): just show what arrived
            std::cout << "\r\033[K" << ui::tag("*", ui::AERO_SKY) << ' '
                      << ui::paint("fetching", ui::BOLD) << "  "
                      << ui::paint(humanReadableSize(now) + " received", ui::ICE_WHITE) << std::flush;
        }
        lastDraw = clock::now();
    }

    if ( showProgress )
        ui::clearLine();

    if ( line.starts_with("failed:") ) {
        ui::fail("fetch failed: " + line.substr(strlen("failed:")));
        return 1;
    }

    // the first non-PROGRESS message is the hash
    const auto hash = line;
    const bool httpExists = std::stoi(connection.receiveInternal()) != 0;

    std::string httpLink;
    if ( httpExists ) {
        connection.sendInternal("getHttpLink");
        httpLink = connection.receiveInternal();
    }

    if ( quiet ) {
        std::cout << "hash: " << hash << '\n';
        if ( httpExists ) std::cout << "http: " << httpLink << "\n\n";
        return 0;
    }

    ui::printTree(ui::tag("+", ui::AQUA_GLASS) + " " + ui::paint("fetched", ui::BOLD), {
                      { "hash", hash,                               ui::AERO_SKY },
                      { "link", httpExists ? httpLink : "not available",
                              httpExists ? ui::LUMINOUS_GLASS : ui::FROSTED_STEEL },
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
        std::cout << ui::tag("-", ui::FROSTED_STEEL) << ' ' << ui::paint("no files found", ui::FROSTED_STEEL) << '\n';
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
    std::cout << '\n' << ui::paint("::", ui::AERO_SKY) << ' ' << ui::paint("files", ui::BOLD)
              << ui::paint(" (" + std::to_string(files.size()) + ")  // " + humanReadableSize(total) + " total", ui::FROSTED_STEEL)
              << "\n\n";

    std::string head = pad + ui::paint("name", ui::FROSTED_STEEL) + ui::spaces(wName - 4) + gap
                     + ui::spaces(wSize - 4) + ui::paint("size", ui::FROSTED_STEEL) + gap
                     + ui::paint("uploaded", ui::FROSTED_STEEL) + ui::spaces(wDate - 8);
    std::string rule = pad + ui::paint(ui::repeat("─", wName), ui::FROSTED_STEEL) + gap
                     + ui::paint(ui::repeat("─", wSize), ui::FROSTED_STEEL) + gap
                     + ui::paint(ui::repeat("─", wDate), ui::FROSTED_STEEL);
    if ( inlineHash ) {
        head += gap + ui::paint("hash", ui::FROSTED_STEEL);
        rule += gap + ui::paint(ui::repeat("─", wHash), ui::FROSTED_STEEL);
    }
    std::cout << head << '\n' << rule << '\n';

    // ── Rows ─────────────────────────────────────────────────────────────
    for ( const auto& r : rows ) {
        std::cout << pad << ui::styledName(r.name) << ui::spaces(wName - ui::displayWidth(r.name)) << gap
                  << ui::spaces(wSize - ui::displayWidth(r.size)) << ui::paint(r.size, r.sizeCol) << gap
                  << ui::paint(r.date, ui::FROSTED_STEEL);
        if ( inlineHash ) {
            std::cout << ui::spaces(wDate - ui::displayWidth(r.date)) << gap << ui::paint(r.hash, ui::AERO_SKY) << '\n';
        } else {
            std::cout << '\n' << pad << ui::paint("└─ ", ui::FROSTED_STEEL) << ui::paint(r.hash, ui::AERO_SKY) << '\n';
        }
    }

    std::cout << std::endl;
    return 0;
}
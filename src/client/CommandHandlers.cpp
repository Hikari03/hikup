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
#include "util.cpp"
#include "../shared/FileInfo.hpp"

namespace {

// ── Palette (change the look here, and only here) ────────────────────────
const bool g_tty   = isatty(STDOUT_FILENO);
const bool g_color = g_tty && !std::getenv("NO_COLOR");

constexpr auto RUST  = "\033[38;5;166m";   // accent: prompts, bars, hashes
constexpr auto AMBER = "\033[38;5;214m";   // sizes, links
constexpr auto MOSS  = "\033[38;5;107m";   // success
constexpr auto SAND  = "\033[38;5;180m";   // names, plain values
constexpr auto ASH   = "\033[38;5;243m";   // chrome, labels, secondary
constexpr auto BRICK = "\033[38;5;160m";   // errors
constexpr auto BOLD  = "\033[1m";

std::string paint(const std::string& s, const char* code) {
    return g_color ? std::string(code) + s + "\033[0m" : s;
}

// ── Text helpers (width math always on PLAIN text) ───────────────────────
size_t displayWidth(const std::string& s) {
    size_t n = 0;
    for ( unsigned char c : s ) if ( (c & 0xC0) != 0x80 ) ++n;
    return n;
}

std::string spaces(size_t n) { return std::string(n, ' '); }

std::string repeat(const std::string& s, size_t n) {
    std::string out;
    for ( size_t i = 0; i < n; ++i ) out += s;
    return out;
}

size_t terminalWidth() {   // 0 when piped
    winsize ws{};
    if ( !isatty(STDOUT_FILENO) || ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0 ) return 0;
    return ws.ws_col;
}

std::string formatDuration(double s) {
    char buf[32];
    if ( s < 60 )        std::snprintf(buf, sizeof buf, "%.1fs", s);
    else if ( s < 3600 ) std::snprintf(buf, sizeof buf, "%dm %02ds", int(s) / 60, int(s) % 60);
    else                 std::snprintf(buf, sizeof buf, "%dh %02dm", int(s) / 3600, (int(s) % 3600) / 60);
    return buf;
}

std::string styledName(const std::string& name) {          // stem sand, ".ext" ash
    const size_t dot = name.find_last_of('.');
    if ( dot == std::string::npos || dot == 0 ) return paint(name, SAND);
    return paint(name.substr(0, dot), SAND) + paint(name.substr(dot), ASH);
}

const char* sizeColor(double bytes) {
    if ( bytes < 1024.0 * 1024 )       return MOSS;
    if ( bytes < 100.0 * 1024 * 1024 ) return AMBER;
    return RUST;
}

// ── Status tags: [+] ok   [*] busy   [!] error   [-] nothing ─────────────
std::string tag(const char* sym, const char* color) {
    return paint("[", ASH) + paint(sym, color) + paint("]", ASH);
}

void fail(const std::string& msg) {
    std::cerr << tag("!", BRICK) << ' ' << msg << std::endl;
}

// ── Progress line (shared by upload & download) ──────────────────────────
//  [*] uploading  [████████████░░░░░░░░░░░░]  61%  497.8 MB/812.0 MB  38.2 MB/s  eta 8.2s
void drawProgress(const std::string& verb, unsigned long long done, unsigned long long total,
                  double speed, size_t termW) {
    const double frac = total ? std::min(1.0, double(done) / double(total)) : 1.0;

    char pct[8];
    std::snprintf(pct, sizeof pct, "%3.0f%%", frac * 100.0);

    const std::string sizes = humanReadableSize(done) + "/" + humanReadableSize(total);
    const std::string spd   = humanReadableSpeed(speed);
    const std::string eta   = speed > 0 ? "eta " + formatDuration(double(total - done) / speed) : "eta --";

    // "[*] " + verb + "  " + "[bar]" + "  " + pct + "  " + sizes + "  " + spd + "  " + eta
    const size_t fixed = 4 + displayWidth(verb) + 2 + 2 + 2 + 4 + 2 + displayWidth(sizes)
                       + 2 + displayWidth(spd) + 2 + displayWidth(eta);
    const size_t barW  = termW > fixed + 8 ? std::min<size_t>(24, termW - fixed - 1) : 8;
    const size_t fill  = static_cast<size_t>(std::lround(frac * double(barW)));

    std::cout << "\r\033[K" << tag("*", RUST) << ' ' << paint(verb, BOLD) << "  "
              << paint("[", ASH) << paint(repeat("█", fill), RUST) << paint(repeat("░", barW - fill), ASH)
              << paint("]", ASH) << "  "
              << paint(pct, RUST) << "  "
              << paint(sizes, SAND) << "  "
              << paint(spd, MOSS) << "  "
              << paint(eta, ASH) << std::flush;
}

void clearLine() { std::cout << "\r\033[K" << std::flush; }

// ── Result tree ──────────────────────────────────────────────────────────
//  [+] uploaded 812.0 MB
//   ├─ time  21.3s @ 38.1 MB/s
//   └─ hash  7bc4e9...
struct Field { std::string label, value; const char* color; };

void printTree(const std::string& header, const std::vector<Field>& fields) {
    size_t lw = 0;
    for ( auto& f : fields ) lw = std::max(lw, displayWidth(f.label));

    std::cout << '\n' << header << '\n';
    for ( size_t i = 0; i < fields.size(); ++i ) {
        const auto& f = fields[i];
        std::cout << ' ' << paint(i + 1 == fields.size() ? "└─ " : "├─ ", ASH)
                  << paint(f.label + spaces(lw - displayWidth(f.label)), ASH) << "  "
                  << paint(f.value, f.color) << '\n';
    }
    std::cout << '\n';
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════
void CommandHandlers::sendFile ( std::ifstream& file, const std::ifstream::pos_type fileSize, Connection& connection, const bool quiet ) {
    if ( !file.good() ) { fail("could not open file"); return; }

    using clock = std::chrono::steady_clock;
    const auto total = static_cast<unsigned long long>(fileSize);

    const unsigned long freeRam = std::clamp(static_cast<unsigned long>(getFreeMemory() / 4),
                                             static_cast<unsigned long>(8 * 1024 * 1024),
                                             static_cast<unsigned long>(64 * 1024 * 1024));

    size_t chunkSize = 8 * 1024 * 1024;
    std::vector<char> buffer(chunkSize);

    double upTime = 0.0;
    unsigned long long sent = 0;

    const bool showProgress = !quiet && g_tty;
    const size_t termW = terminalWidth();

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
            drawProgress("uploading", sent, total, upTime > 0 ? double(sent) / upTime : 0.0, termW);
            lastDraw = clock::now();
        }

        size_t newSize = chunkSize;
        if ( upDur.count() > 1.0 )                                  newSize = std::max<size_t>(1024 * 1024, chunkSize * 0.75);
        else if ( upDur.count() < 0.2 && freeRam >= chunkSize * 2 ) newSize = chunkSize * 2;
        else if ( upDur.count() < 0.4 && freeRam >= chunkSize * 2 ) newSize = chunkSize * 1.25;

        if ( newSize != chunkSize ) { chunkSize = newSize; buffer.resize(chunkSize); }
    }

    const std::chrono::duration<double> wall = clock::now() - start;
    if ( showProgress ) clearLine();

    if ( sent != total ) {
        fail("upload incomplete: sent " + humanReadableSize(sent) + " of " + humanReadableSize(total));
        return;
    }

    if ( const auto confirmation = connection.receiveInternal(); confirmation != "OK" ) {
        fail("upload failed: " + confirmation);
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
        if ( httpExists ) std::cout << "http: " << httpLink << "\n\n";
        return;
    }

    const double avg = wall.count() > 0 ? double(total) / wall.count() : 0.0;
    std::vector<Field> fields = {
        { "time", formatDuration(wall.count()) + " @ " + humanReadableSpeed(avg), SAND },
        { "hash", hash,                                                          RUST },
        { "link", httpExists ? httpLink : "not available",                       httpExists ? AMBER : ASH },
    };
#ifdef HIKUP_DEBUG
    fields.push_back({ "chunk", humanReadableSize(chunkSize), ASH });
#endif
    printTree(tag("+", MOSS) + " " + paint("uploaded", BOLD) + " " + paint(humanReadableSize(fileSize), AMBER), fields);
}

// ═════════════════════════════════════════════════════════════════════════
void CommandHandlers::downloadFile ( Connection& connection, const bool quiet ) {
    using clock = std::chrono::steady_clock;

    const auto fileSize = std::stoll(connection.receiveInternal());
    const auto total    = static_cast<unsigned long long>(fileSize);

    // never trust a server-supplied name as a path
    const std::string fileName = std::filesystem::path(connection.receiveInternal()).filename().string();
    if ( fileName.empty() || fileName == "." || fileName == ".." ) {
        fail("server sent an invalid file name");
        return;
    }

    const unsigned long freeRam = std::clamp(static_cast<unsigned long>(getFreeMemory() / 4),
                                             static_cast<unsigned long>(4 * 1024 * 1024),
                                             static_cast<unsigned long>(64 * 1024 * 1024));
    connection.resizeBuffer(freeRam);

    std::ofstream file(fileName, std::ios::binary);
    if ( !file ) { fail("could not create " + fileName); return; }

    const bool showProgress = !quiet && g_tty;
    const size_t termW = terminalWidth();
    unsigned long long got = 0;

    if ( !quiet ) {
        std::cout << '\n' << tag("*", RUST) << ' ' << paint("downloading", BOLD) << ' '
                  << styledName(fileName) << '\n';
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
                drawProgress("downloading", got, total, el.count() > 0 ? double(got) / el.count() : 0.0, termW);
                lastDraw = clock::now();
            }
        });
    } catch ( const std::exception& e ) {
        if ( showProgress ) clearLine();
        fail(std::string("download failed: ") + e.what());
        file.close();
        std::error_code ec;
        std::filesystem::remove(fileName, ec);          // no partial files
        return;
    }

    const std::chrono::duration<double> wall = clock::now() - start;
    file.close();
    if ( showProgress ) clearLine();

    connection.sendInternal("OK");

    if ( quiet ) return;

    const double avg = wall.count() > 0 ? double(total) / wall.count() : 0.0;
    printTree(tag("+", MOSS) + " " + paint("downloaded", BOLD) + " " + styledName(fileName), {
        { "size", humanReadableSize(fileSize),                                   AMBER },
        { "time", formatDuration(wall.count()) + " @ " + humanReadableSpeed(avg), SAND  },
    });
}

// ═════════════════════════════════════════════════════════════════════════
int CommandHandlers::listFiles ( Connection& connection, const std::string& user, const std::string& pass ) {
    connection.sendInternal("user:" + user);
    connection.sendInternal("pass:" + pass);

    if ( connection.receiveInternal() != "OK" ) {
        fail("authentication failed");
        return 1;
    }

    std::vector<FileInfo> files;
    try {
        std::string fileData;
        while ( ( fileData = connection.receive() ) != _internal"DONE" ) {
            files.emplace_back(fileData.substr(strlen(_data)));
        }
    } catch ( std::runtime_error& e ) {
        fail(std::string("error receiving file list: ") + e.what());
        return 1;
    }

    if ( files.empty() ) {
        std::cout << tag("-", ASH) << ' ' << paint("no files found", ASH) << '\n';
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
               f.getHash(), sizeColor(static_cast<double>(f.getSize())) };
        wName = std::max(wName, displayWidth(r.name));
        wSize = std::max(wSize, displayWidth(r.size));
        wDate = std::max(wDate, displayWidth(r.date));
        wHash = std::max(wHash, displayWidth(r.hash));
        total += f.getSize();
        rows.push_back(std::move(r));
    }

    // ── Layout: hash inline if it fits, otherwise on its own └─ line ─────
    const size_t gapW = 2, indent = 2;
    const size_t termW = terminalWidth();
    const size_t inlineW = indent + wName + wSize + wDate + wHash + 3 * gapW;
    const bool inlineHash = termW == 0 || inlineW <= termW;
    const std::string gap(gapW, ' '), pad(indent, ' ');

    // ── Header ───────────────────────────────────────────────────────────
    //  :: files (3)  // 813.2 MB total
    std::cout << '\n' << paint("::", RUST) << ' ' << paint("files", BOLD)
              << paint(" (" + std::to_string(files.size()) + ")  // " + humanReadableSize(total) + " total", ASH)
              << "\n\n";

    std::string head = pad + paint("name", ASH) + spaces(wName - 4) + gap
                     + spaces(wSize - 4) + paint("size", ASH) + gap
                     + paint("uploaded", ASH) + spaces(wDate - 8);
    std::string rule = pad + paint(repeat("─", wName), ASH) + gap
                     + paint(repeat("─", wSize), ASH) + gap
                     + paint(repeat("─", wDate), ASH);
    if ( inlineHash ) {
        head += gap + paint("hash", ASH);
        rule += gap + paint(repeat("─", wHash), ASH);
    }
    std::cout << head << '\n' << rule << '\n';

    // ── Rows ─────────────────────────────────────────────────────────────
    for ( const auto& r : rows ) {
        std::cout << pad << styledName(r.name) << spaces(wName - displayWidth(r.name)) << gap
                  << spaces(wSize - displayWidth(r.size)) << paint(r.size, r.sizeCol) << gap
                  << paint(r.date, ASH);
        if ( inlineHash ) {
            std::cout << spaces(wDate - displayWidth(r.date)) << gap << paint(r.hash, RUST) << '\n';
        } else {
            std::cout << '\n' << pad << paint("└─ ", ASH) << paint(r.hash, RUST) << '\n';
        }
    }

    std::cout << std::endl;
    return 0;
}
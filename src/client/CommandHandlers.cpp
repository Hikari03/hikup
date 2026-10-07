#include "CommandHandlers.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <sys/ioctl.h>   // Windows: use GetConsoleScreenBufferInfo instead
#include <unistd.h>      // isatty (Windows: <io.h> and _isatty/_fileno)
#include <vector>

#include "Color.hpp"
#include "util.cpp"
#include "../shared/FileInfo.hpp"

namespace {

    // ── Styling ──────────────────────────────────────────────────────────────
    const bool g_color = isatty(STDOUT_FILENO) && !std::getenv("NO_COLOR");

    constexpr auto DIM    = "\033[90m";
    constexpr auto BOLD   = "\033[1m";
    constexpr auto CYAN   = "\033[36m";
    constexpr auto GREEN  = "\033[32m";
    constexpr auto YELLOW = "\033[33m";
    constexpr auto RED    = "\033[31m";
    constexpr auto PURPLE = "\033[35m";

    std::string paint(const std::string& s, const char* code) {
        return g_color ? std::string(code) + s + "\033[0m" : s;
    }

    // ── Text helpers (all width math is done on PLAIN text) ──────────────────
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

    // Returns 0 when output is not a terminal (piped / redirected)
    size_t terminalWidth() {
        winsize ws{};
        if ( !isatty(STDOUT_FILENO) || ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0 ) return 0;
        return ws.ws_col;
    }

    std::string styledName(const std::string& name) {
        size_t dot = name.find_last_of('.');
        if ( dot == std::string::npos || dot == 0 ) return paint(name, CYAN);
        return paint(name.substr(0, dot), CYAN) + paint(name.substr(dot), DIM);
    }

    const char* sizeColor(double bytes) {
        if ( bytes < 1024.0 * 1024 )       return GREEN;
        if ( bytes < 100.0 * 1024 * 1024 ) return YELLOW;
        return RED;
    }

    // ── Borders ──────────────────────────────────────────────────────────────
    std::string topBorder(const std::string& text, size_t inner) {
        return paint("╭─ ", DIM) + paint(text, BOLD) + paint(" ", DIM)
             + paint(repeat("─", inner - displayWidth(text) - 3) + "╮", DIM) + '\n';
    }

    std::string midBorder(size_t inner) {
        return paint("├" + repeat("─", inner) + "┤", DIM) + '\n';
    }

    std::string bottomBorder(const std::string& text, size_t inner) {
        return paint("╰" + repeat("─", inner - displayWidth(text) - 3) + " " + text + " ─╯", DIM) + '\n';
    }

    // One framed row: │  <content padded to cw>  │
    std::string line(const std::string& styled, size_t visible, size_t cw) {
        const std::string bar = paint("│", DIM);
        return bar + "  " + styled + spaces(cw - visible) + "  " + bar + '\n';
    }

    // ── Additions to the anonymous namespace ─────────────────────────────────
    constexpr auto BLUE = "\033[94m";
    const bool g_tty = isatty(STDOUT_FILENO);

    std::string progressBar(const double frac, const size_t width) {
        const size_t filled = static_cast<size_t>(std::lround(std::clamp(frac, 0.0, 1.0) * width));
        return paint(repeat("━", filled), CYAN) + paint(repeat("─", width - filled), DIM);
    }

    std::string formatDuration(double s) {
        char buf[32];
        if ( s < 60 )        std::snprintf(buf, sizeof buf, "%.1f s", s);
        else if ( s < 3600 ) std::snprintf(buf, sizeof buf, "%dm %02ds", static_cast<int>(s) / 60, static_cast<int>(s) % 60);
        else                 std::snprintf(buf, sizeof buf, "%dh %02dm", static_cast<int>(s) / 3600, (static_cast<int>(s) % 3600) / 60);
        return buf;
    }

    struct Field { std::string label, value; const char* color; };

    void printCard(const std::string& title, const std::vector<Field>& fields) {
        size_t labelW = 0, cw = displayWidth(title);
        for ( auto& f : fields ) labelW = std::max(labelW, displayWidth(f.label));
        for ( auto& f : fields ) cw = std::max(cw, labelW + 3 + displayWidth(f.value));
        const size_t inner = cw + 4;

        std::cout << '\n' << topBorder(title, inner);
        for ( auto& f : fields ) {
            std::string styled = paint(f.label, DIM) + spaces(labelW - displayWidth(f.label)) + "   "
                               + paint(f.value, f.color);
            std::cout << line(styled, labelW + 3 + displayWidth(f.value), cw);
        }
        std::cout << paint("╰" + repeat("─", inner) + "╯", DIM) << "\n\n";
    }

} // namespace

void CommandHandlers::sendFile ( std::ifstream& file, const std::ifstream::pos_type fileSize, Connection& connection, const bool quiet ) {
    if ( !file.good() ) {
        std::cerr << paint("✘ Could not open file", RED) << std::endl;
        return;
    }

    using clock = std::chrono::steady_clock;
    const auto total = static_cast<unsigned long long>(fileSize);

    const unsigned long freeRam = std::clamp(static_cast<unsigned long>(getFreeMemory() / 4),
                                             static_cast<unsigned long>(8 * 1024 * 1024),
                                             static_cast<unsigned long>(64 * 1024 * 1024));

    size_t chunkSize = 8 * 1024 * 1024;
    std::vector<char> buffer(chunkSize);

    double totalTimeUpload = 0.0;
    unsigned long long sizeUploaded = 0;

    const bool showProgress = !quiet && g_tty;
    const size_t termW = terminalWidth();

    // ── Progress line ────────────────────────────────────────────────────
    auto draw = [&] {
        const double frac  = total ? std::min(1.0, double(sizeUploaded) / total) : 1.0;
        const double speed = totalTimeUpload > 0 ? sizeUploaded / totalTimeUpload : 0.0;

        char pctBuf[16];
        std::snprintf(pctBuf, sizeof pctBuf, "%5.1f%%", frac * 100.0);

        const std::string sizes = humanReadableSize(sizeUploaded) + " / " + humanReadableSize(total);
        const std::string spd   = humanReadableSpeed(speed);
        const std::string eta   = speed > 0 ? "ETA " + formatDuration((total - sizeUploaded) / speed) : "ETA --";

        // fixed text = "  Uploading  " + bar + "  pct  sizes  speed  eta"
        const size_t fixed = 13 + 2 + 6 + 2 + displayWidth(sizes) + 2 + displayWidth(spd) + 2 + displayWidth(eta);
        const size_t barW  = termW > fixed + 8 ? std::min<size_t>(30, termW - fixed - 1) : 8;

        std::cout << "\r\033[K  " << paint("Uploading", BLUE) << "  "
                  << progressBar(frac, barW) << "  "
                  << paint(pctBuf, PURPLE) << "  "
                  << paint(sizes, CYAN) << "  "
                  << paint(spd, GREEN) << "  "
                  << paint(eta, DIM)
#ifdef HIKUP_DEBUG
                  << paint("  chunk " + humanReadableSize(chunkSize), DIM)
#endif
                  << std::flush;
    };

    auto clearLine = [&] { if ( showProgress ) std::cout << "\r\033[K" << std::flush; };

    if ( !quiet ) {
        std::cout << '\n' << paint("▸ ", BLUE) << paint("Uploading ", BOLD)
                  << paint(humanReadableSize(fileSize), CYAN) << "\n";
    }

    // ── Transfer loop ────────────────────────────────────────────────────
    const auto startAll = clock::now();
    auto lastDraw = startAll - std::chrono::seconds(1);
    unsigned long long sizeRead = 0;

    while ( sizeRead < total ) {
        file.read(buffer.data(), static_cast<std::streamsize>(chunkSize));
        const auto got = static_cast<size_t>(file.gcount());
        if ( got == 0 ) break;                       // file shorter than reported / read error
        sizeRead += got;

        const auto t0 = clock::now();
        connection.sendRaw(buffer.data(), got);
        const std::chrono::duration<double> upDur = clock::now() - t0;

        totalTimeUpload += upDur.count();
        sizeUploaded    += got;

        if ( showProgress && clock::now() - lastDraw >= std::chrono::milliseconds(80) ) {
            draw();
            lastDraw = clock::now();
        }

        // adaptive chunk size (same thresholds as before)
        size_t newSize = chunkSize;
        if ( upDur.count() > 1.0 )                                   newSize = std::max<size_t>(1024 * 1024, chunkSize * 0.75);
        else if ( upDur.count() < 0.2 && freeRam >= chunkSize * 2 )  newSize = chunkSize * 2;
        else if ( upDur.count() < 0.4 && freeRam >= chunkSize * 2 )  newSize = chunkSize * 1.25;

        if ( newSize != chunkSize ) {
            chunkSize = newSize;
            buffer.resize(chunkSize);
        }
    }

    const std::chrono::duration<double> wall = clock::now() - startAll;
    clearLine();

    if ( sizeUploaded != total ) {
        std::cerr << paint("✘ Upload incomplete: sent " + humanReadableSize(sizeUploaded)
                         + " of " + humanReadableSize(total), RED) << std::endl;
        return;
    }

    // ── Server response ──────────────────────────────────────────────────
    if ( const auto confirmation = connection.receiveInternal(); confirmation != "OK" ) {
        std::cerr << paint("✘ Upload failed: " + confirmation, RED) << std::endl;
        return;
    }

    const auto hash = connection.receiveInternal();
    const bool httpExists = std::stoi(connection.receiveInternal()) != 0;

    std::string httpLink;
    if ( httpExists ) {
        connection.sendInternal("getHttpLink");
        httpLink = connection.receiveInternal();
    }

    // ── Output ───────────────────────────────────────────────────────────
    if ( quiet ) {
        std::cout << "hash: " << hash << '\n';
        if ( httpExists ) std::cout << "http: " << httpLink << "\n\n";
        return;
    }

    const double avg = wall.count() > 0 ? total / wall.count() : 0.0;
    std::vector<Field> fields = {
        { "Size", humanReadableSize(fileSize),                                  CYAN   },
        { "Time", formatDuration(wall.count()) + "  ·  " + humanReadableSpeed(avg) + " avg", GREEN },
        { "Hash", hash,                                                         PURPLE },
        { "Link", httpExists ? httpLink : "not available",                      httpExists ? CYAN : DIM },
    };
#ifdef HIKUP_DEBUG
    fields.push_back({ "Chunk", humanReadableSize(chunkSize) + " (final)", DIM });
#endif
    printCard(paint("✔", GREEN).empty() ? "Upload complete" : "✔ Upload complete", fields);
}

void CommandHandlers::downloadFile ( Connection& connection, const bool quiet ) {
    auto fileSize = std::stoll(connection.receiveInternal());
    auto fileName = connection.receiveInternal();

    const auto freeRamLimit = 64 * 1024 * 1024; // 64 MB
    const auto freeRam = std::clamp(static_cast<unsigned long>(getFreeMemory() / 4), static_cast<unsigned long>(4 * 1024 * 1024), static_cast<unsigned long>(freeRamLimit));

    connection.resizeBuffer(freeRam);

    unsigned long long sizeDownloaded = 0;
    const auto startOverall = std::chrono::high_resolution_clock::now();

    if ( !quiet ) {
        std::cout << colorize("Downloading file: ", Color::BLUE) + colorize(fileName, Color::CYAN) << colorize(
            " of size: ", Color::BLUE
        ) << colorize(humanReadableSize(fileSize), Color::CYAN) << std::endl;
    }

    // create file
    std::ofstream file(fileName, std::ios::binary);

    try {
        connection.receiveExact(fileSize, [&](const char* chunk, size_t size) {
            file.write(chunk, size);
            sizeDownloaded += size;

            if ( !quiet ) {
                const auto now = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double> totalElapsed = now - startOverall;
                double downloadSpeed = static_cast<double>(sizeDownloaded) / totalElapsed.count();

                std::cout << "\r" << colorize("Receiving data: ", Color::BLUE) <<
                        colorize(humanReadableSize(sizeDownloaded), Color::CYAN) << colorize("/", Color::BLUE) <<
                        colorize(humanReadableSize(fileSize), Color::CYAN) << colorize(
                            std::string(" (") +
                            std::to_string(( static_cast<double>(sizeDownloaded) / static_cast<double>(fileSize) ) * 100.0).
                            substr(0, 5) + " %)",
                            Color::PURPLE
                        ) << " ┃ " << colorize("Speed: " + humanReadableSpeed(downloadSpeed), Color::GREEN) << "  " << std::flush;
            }
        });
    }
    catch ( const std::exception& e ) {
        std::cerr << "\n" << colorize("Error downloading file: ", Color::RED) << colorize(e.what(), Color::RED) << std::endl;
        file.close();
        return;
    }

    connection.sendInternal("OK");

    if ( !quiet ) {
        std::cout << std::endl;
    }
}



int CommandHandlers::listFiles ( Connection& connection, const std::string& user, const std::string& pass ) {
    connection.sendInternal("user:" + user);
    connection.sendInternal("pass:" + pass);

    if ( connection.receiveInternal() != "OK" ) {
        std::cerr << colorize("Authentication failed", Color::RED) << std::endl;
        return 1;
    }

    std::vector<FileInfo> files;
    try {
        std::string fileData;
        while ( ( fileData = connection.receive() ) != _internal"DONE" ) {
            files.emplace_back(fileData.substr(strlen(_data)));
        }
    } catch ( std::runtime_error& e ) {
        std::cerr << colorize(std::string("Error receiving file list: ") + e.what(), Color::RED) << std::endl;
        return 1;
    }

    if ( files.empty() ) {
        std::cout << paint("∅ No files found.", DIM) << "\n";
        return 0;
    }

    // ── Build rows & measure ─────────────────────────────────────────────
    struct Row { std::string name, size, date, hash; const char* sizeCol; };
    size_t wName = 4, wSize = 4, wDate = 8, wHash = 4;
    std::vector<Row> rows;
    rows.reserve(files.size());

    decltype(files[0].getSize()) total{};
    for ( auto& f : files ) {
        Row r{ f.getName(), humanReadableSize(f.getSize()),
               f.getCreationDateString_c(), f.getHash(),
               sizeColor(static_cast<double>(f.getSize())) };
        wName = std::max(wName, displayWidth(r.name));
        wSize = std::max(wSize, displayWidth(r.size));
        wDate = std::max(wDate, displayWidth(r.date));
        wHash = std::max(wHash, displayWidth(r.hash));
        total += f.getSize();
        rows.push_back(std::move(r));
    }

    // ── Pick a layout ────────────────────────────────────────────────────
    // Content width = the space between "│  " and "  │"
    const size_t gapW      = 3;
    const size_t inlineCw  = wName + wSize + wDate + wHash + 3 * gapW;
    const size_t termW     = terminalWidth();
    const bool   inlineHash = termW == 0 || inlineCw + 6 <= termW;

    size_t cw = inlineHash
        ? inlineCw
        : std::max(wName + wSize + wDate + 2 * gapW, wHash + 2);   // +2 for "╰ "

    const std::string title  = "Files · " + std::to_string(files.size());
    const std::string footer = humanReadableSize(total) + " total";
    cw = std::max({ cw, displayWidth(title), displayWidth(footer) });
    const size_t inner = cw + 4;
    const std::string gap(gapW, ' ');

    // ── Render ───────────────────────────────────────────────────────────
    std::cout << '\n' << topBorder(title, inner);

    // Header
    {
        std::string styled = paint("NAME", BOLD) + spaces(wName - 4) + gap
                           + spaces(wSize - 4) + paint("SIZE", BOLD) + gap
                           + paint("UPLOADED", BOLD) + spaces(wDate - 8);
        size_t visible = wName + gapW + wSize + gapW + wDate;
        if ( inlineHash ) {
            styled += gap + paint("HASH", BOLD) + spaces(wHash - 4);
            visible += gapW + wHash;
        }
        std::cout << line(styled, visible, cw) << midBorder(inner);
    }

    // Body
    for ( const auto& r : rows ) {
        std::string styled = styledName(r.name) + spaces(wName - displayWidth(r.name)) + gap
                           + spaces(wSize - displayWidth(r.size)) + paint(r.size, r.sizeCol) + gap
                           + paint(r.date, DIM) + spaces(wDate - displayWidth(r.date));
        size_t visible = wName + gapW + wSize + gapW + wDate;

        if ( inlineHash ) {
            styled += gap + paint(r.hash, PURPLE) + spaces(wHash - displayWidth(r.hash));
            visible += gapW + wHash;
            std::cout << line(styled, visible, cw);
        } else {
            std::cout << line(styled, visible, cw);
            std::cout << line(paint("╰ ", DIM) + paint(r.hash, PURPLE), 2 + displayWidth(r.hash), cw);
        }
    }

    std::cout << bottomBorder(footer, inner) << '\n';
    std::cout.flush();
    return 0;
}
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>      // isatty (Windows: <io.h>, _isatty/_fileno)
#include <vector>
#include <sys/ioctl.h>   // Windows: GetConsoleScreenBufferInfo

namespace ui {

// ── Palette (change the look here, and only here) ────────────────────────
inline const bool g_tty   = isatty(STDOUT_FILENO);
inline const bool g_color = g_tty && !std::getenv("NO_COLOR");

constexpr auto RUST  = "\033[38;5;166m";   // accent: prompts, bars, hashes
constexpr auto AMBER = "\033[38;5;214m";   // sizes, links
constexpr auto MOSS  = "\033[38;5;107m";   // success
constexpr auto SAND  = "\033[38;5;180m";   // names, plain values
constexpr auto ASH   = "\033[38;5;243m";   // chrome, labels, secondary
constexpr auto BRICK = "\033[38;5;160m";   // errors
constexpr auto BOLD  = "\033[1m";

inline std::string paint(const std::string& s, const char* code) {
    return g_color ? std::string(code) + s + "\033[0m" : s;
}

// ── Text helpers (width math always on PLAIN text) ───────────────────────
inline size_t displayWidth(const std::string& s) {
    size_t n = 0;
    for ( unsigned char c : s ) if ( (c & 0xC0) != 0x80 ) ++n;
    return n;
}

inline std::string spaces(size_t n) { return std::string(n, ' '); }

inline std::string repeat(const std::string& s, size_t n) {
    std::string out;
    for ( size_t i = 0; i < n; ++i ) out += s;
    return out;
}

inline size_t terminalWidth() {   // 0 when piped
    winsize ws{};
    if ( !isatty(STDOUT_FILENO) || ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0 ) return 0;
    return ws.ws_col;
}

inline std::string formatDuration(double s) {
    char buf[32];
    if ( s < 60 )        std::snprintf(buf, sizeof buf, "%.1fs", s);
    else if ( s < 3600 ) std::snprintf(buf, sizeof buf, "%dm %02ds", int(s) / 60, int(s) % 60);
    else                 std::snprintf(buf, sizeof buf, "%dh %02dm", int(s) / 3600, (int(s) % 3600) / 60);
    return buf;
}

inline std::string styledName(const std::string& name) {   // stem sand, ".ext" ash
    const size_t dot = name.find_last_of('.');
    if ( dot == std::string::npos || dot == 0 ) return paint(name, SAND);
    return paint(name.substr(0, dot), SAND) + paint(name.substr(dot), ASH);
}

inline const char* sizeColor(double bytes) {
    if ( bytes < 1024.0 * 1024 )       return MOSS;
    if ( bytes < 100.0 * 1024 * 1024 ) return AMBER;
    return RUST;
}

// ── Status tags: [+] ok   [*] busy   [!] error   [-] nothing ─────────────
inline std::string tag(const char* sym, const char* color) {
    return paint("[", ASH) + paint(sym, color) + paint("]", ASH);
}

inline void step(const std::string& msg, const std::string& detail = "") {
    std::cout << tag("*", RUST) << ' ' << msg
              << (detail.empty() ? "" : "  " + paint(detail, ASH)) << '\n';
}
inline void done(const std::string& msg, const std::string& detail = "") {
    std::cout << tag("+", MOSS) << ' ' << msg
              << (detail.empty() ? "" : "  " + paint(detail, ASH)) << '\n';
}
inline void fail(const std::string& msg) {
    std::cerr << tag("!", BRICK) << ' ' << msg << std::endl;
}

    // ── Progress line (shared by hash / upload / download) ───────────────────
    //  [*] hashing  [██████████░░░░░░░░░░░░░░]  41%  332.0 MB/812.0 MB  610.2 MB/s  eta 0.8s
    inline void drawBar(const std::string& verb, double frac, const std::string& sizes,
                        const std::string& speed, const std::string& eta, size_t termW) {
    frac = std::clamp(frac, 0.0, 1.0);

    char pct[8];
    std::snprintf(pct, sizeof pct, "%3.0f%%", frac * 100.0);

    // "[*] " + verb + "  " + "[bar]" + "  " + pct + "  " + sizes + "  " + speed + "  " + eta
    const size_t fixed = 4 + displayWidth(verb) + 2 + 2 + 2 + 4 + 2 + displayWidth(sizes)
                       + 2 + displayWidth(speed) + 2 + displayWidth(eta);
    const size_t barW  = termW > fixed + 8 ? std::min<size_t>(24, termW - fixed - 1) : 8;
    const size_t fill  = static_cast<size_t>(std::lround(frac * double(barW)));

    std::cout << "\r\033[K" << tag("*", RUST) << ' ' << paint(verb, BOLD) << "  "
              << paint("[", ASH) << paint(repeat("█", fill), RUST) << paint(repeat("░", barW - fill), ASH)
              << paint("]", ASH) << "  "
              << paint(pct, RUST) << "  "
              << paint(sizes, SAND) << "  "
              << paint(speed, MOSS) << "  "
              << paint(eta, ASH) << std::flush;
}

inline void clearLine() { std::cout << "\r\033[K" << std::flush; }

// ── Result tree ──────────────────────────────────────────────────────────
//  [+] uploaded 812.0 MB
//   ├─ time  21.3s @ 38.1 MB/s
//   └─ hash  7bc4e9...
struct Field { std::string label, value; const char* color; };

inline void printTree(const std::string& header, const std::vector<Field>& fields,
                      std::ostream& out = std::cout) {
    size_t lw = 0;
    for ( auto& f : fields ) lw = std::max(lw, displayWidth(f.label));

    out << '\n' << header << '\n';
    for ( size_t i = 0; i < fields.size(); ++i ) {
        const auto& f = fields[i];
        out << ' ' << paint(i + 1 == fields.size() ? "└─ " : "├─ ", ASH)
            << paint(f.label + spaces(lw - displayWidth(f.label)), ASH) << "  "
            << paint(f.value, f.color) << '\n';
    }
    out << '\n';
}

} // namespace ui
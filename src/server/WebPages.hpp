#pragma once

// Self-contained HTML pages for the hikup web UI — no external assets.
// Templates use {} placeholders filled with std::format; the CSS and JS
// snippets hold no placeholders, so they live in plain string constants
// passed as arguments and their braces never collide with format syntax.

#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace WebPages {

// one file in the listing — raw values only, escaping happens in listingPage()
struct FileRow {
	std::string name;    // client-style display name, dots restored
	std::string hash;    // hex
	std::string urlExt;  // ".jpg" or ".noext"
	uintmax_t size{};
	std::time_t date{};
};

// ── shared chrome (aero glass, matches the client's look) ────────────────

inline constexpr std::string_view BASE_CSS = R"CSS(:root{--sky:#38bdf8;--deep:#0ea5e9;--pale:#d7f3ff;--ice:#e9f6ff;--steel:#8aabbd;
--aqua:#7bf1c4;--rose:#ff9aa8;--glass:rgba(148,197,233,.07);--line:rgba(148,197,233,.16)}
*{box-sizing:border-box;margin:0;padding:0}
html,body{min-height:100%}
body{font:15px/1.5 system-ui,"Segoe UI",sans-serif;color:var(--ice);
background:radial-gradient(1100px 700px at 75% -10%,#1d5f8f 0%,transparent 60%),
radial-gradient(900px 600px at -10% 110%,#123a5c 0%,transparent 55%),
linear-gradient(160deg,#0a1626,#0d2136 55%,#0a1a2c);background-attachment:fixed}
a{color:var(--sky)}
input,button{font:inherit;color:inherit}
.wordmark{display:flex;align-items:center;gap:10px;user-select:none}
.wordmark svg{filter:drop-shadow(0 0 6px rgba(56,189,248,.55))}
.wordmark b{font-size:22px;letter-spacing:.5px;color:var(--ice)}
.wordmark span{color:var(--steel);font-size:13px;margin-top:5px}
.card{background:var(--glass);border:1px solid var(--line);border-radius:16px;
backdrop-filter:blur(14px);-webkit-backdrop-filter:blur(14px);
box-shadow:0 18px 50px rgba(2,8,20,.45),inset 0 1px 0 rgba(233,246,255,.07)}
.btn{display:inline-flex;align-items:center;gap:6px;padding:5px 12px;border-radius:9px;
border:1px solid var(--line);background:rgba(56,189,248,.08);color:var(--pale);
text-decoration:none;cursor:pointer;font-size:13px;transition:all .15s}
.btn:hover{border-color:rgba(56,189,248,.55);background:rgba(56,189,248,.16)}
.btn.primary{border-color:transparent;background:linear-gradient(135deg,var(--deep),var(--sky));
color:#06283d;font-weight:600}
.btn.primary:hover{filter:brightness(1.12)}
.btn.ghost{background:transparent;color:var(--steel)}
.btn.ghost:hover{color:var(--pale)})CSS";

inline constexpr std::string_view FAVICON =
	"data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'%3E"
	"%3Ccircle cx='16' cy='16' r='14' fill='%230ea5e9'/%3E"
	"%3Cpath d='M16 24V10m0 0l-5 5m5-5l5 5' stroke='%23e9f6ff' stroke-width='3' "
	"stroke-linecap='round' stroke-linejoin='round' fill='none'/%3E%3C/svg%3E";

inline constexpr std::string_view LOGO_SVG =
	R"SVG(<svg width="30" height="30" viewBox="0 0 32 32" aria-hidden="true">
<circle cx="16" cy="16" r="14" fill="#0ea5e9"/>
<path d="M16 24V10m0 0l-5 5m5-5l5 5" stroke="#e9f6ff" stroke-width="3"
stroke-linecap="round" stroke-linejoin="round" fill="none"/></svg>)SVG";

// ── text helpers ─────────────────────────────────────────────────────────

inline std::string htmlEscape ( const std::string& in ) {
	std::string out;
	out.reserve(in.size());
	for ( const char ch : in ) {
		switch ( ch ) {
			case '&':  out += "&amp;";  break;
			case '<':  out += "&lt;";   break;
			case '>':  out += "&gt;";   break;
			case '"':  out += "&quot;"; break;
			case '\'': out += "&#39;";  break;
			default:   out += ch;
		}
	}
	return out;
}

// encode only the characters that change meaning inside an href
inline std::string urlEncode ( const std::string& in ) {
	std::string out;
	out.reserve(in.size() * 3);
	for ( const unsigned char ch : in ) {
		if ( std::isalnum(ch) || ch == '.' || ch == '-' || ch == '_' || ch == '~' )
			out += static_cast<char>(ch);
		else {
			char buf[8];
			std::snprintf(buf, sizeof buf, "%%%02X", ch);
			out += buf;
		}
	}
	return out;
}

inline std::string humanSize ( const double bytes ) {
	const char* units[] = {"B", "KB", "MB", "GB", "TB"};
	auto size = bytes;
	int unit = 0;
	while ( size >= 1000.0 && unit < 4 ) {
		size /= 1000.0;
		++unit;
	}

	char buf[64];
	std::snprintf(buf, sizeof buf, "%.2f %s", size, units[unit]);
	return buf;
}

inline std::string formatDate ( const std::time_t t ) {
	const auto* tm = std::localtime(&t);
	if ( tm == nullptr )
		return {};

	char buf[24];
	if ( !std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", tm) )
		return {};
	return buf;
}

// ── sign-in page ─────────────────────────────────────────────────────────

inline constexpr std::string_view LOGIN_CSS = R"CSS(main{max-width:360px;margin:14vh auto 0;padding:36px 34px 30px}
form{margin-top:26px;display:flex;flex-direction:column;gap:14px}
label{display:flex;flex-direction:column;gap:6px;font-size:12px;color:var(--steel);
text-transform:lowercase;letter-spacing:.4px}
input{background:rgba(10,22,38,.55);border:1px solid var(--line);border-radius:10px;
padding:10px 12px;color:var(--ice);outline:none;transition:border-color .15s}
input:focus{border-color:rgba(56,189,248,.6)}
button[type=submit]{margin-top:8px;justify-content:center;padding:10px;
font-size:15px;border:none;border-radius:10px;cursor:pointer;
background:linear-gradient(135deg,var(--deep),var(--sky));color:#06283d;font-weight:600;
transition:filter .15s}
button[type=submit]:hover{filter:brightness(1.12)}
.error{margin-top:20px;padding:9px 12px;border-radius:9px;font-size:13px;
border:1px solid rgba(255,154,168,.35);background:rgba(255,154,168,.09);color:var(--rose)})CSS";

inline constexpr std::string_view LOGIN_PAGE = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>hikup &middot; sign in</title>
<link rel="icon" href="{}">
<style>{}
{}</style>
</head>
<body>
<main class="card">
	<div class="wordmark">{}<b>hikup</b><span>file server</span></div>
	{}
	<form method="post" action="/login">
		<label>username <input name="user" autocomplete="username" required autofocus></label>
		<label>password <input name="pass" type="password" autocomplete="current-password" required></label>
		<button type="submit">sign in</button>
	</form>
</main>
</body>
</html>
)HTML";

inline std::string loginPage ( const bool failed ) {
	const auto error = failed ? "<div class='error'>wrong username or password</div>" : "";
	return std::format(LOGIN_PAGE, FAVICON, BASE_CSS, LOGIN_CSS, LOGO_SVG, error);
}

// ── error page ───────────────────────────────────────────────────────────

inline constexpr std::string_view ERROR_CSS = R"CSS(main{max-width:360px;margin:14vh auto 0;padding:36px 34px 30px;text-align:center}
.code{font-size:72px;font-weight:700;line-height:1;margin:22px 0 6px;
background:linear-gradient(135deg,#ff9aa8,#e9f6ff);-webkit-background-clip:text;
background-clip:text;color:transparent;filter:drop-shadow(0 0 14px rgba(255,154,168,.25))}
.title{font-size:17px;color:var(--ice)}
.detail{margin-top:8px;font-size:13px;color:var(--steel)}
main .btn{margin-top:24px})CSS";

inline constexpr std::string_view ERROR_PAGE = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>hikup &middot; {}</title>
<link rel="icon" href="{}">
<style>{}
{}</style>
</head>
<body>
<main class="card">
	<div class="wordmark" style="justify-content:center">{}<b>hikup</b><span>file server</span></div>
	<div class="code">{}</div>
	<div class="title">{}</div>
	<div class="detail">{}</div>
	<a class="btn" href="/">files</a>
</main>
</body>
</html>
)HTML";

inline std::string errorPage ( const int status, const std::string& title, const std::string& detail ) {
	return std::format(ERROR_PAGE, status, FAVICON, BASE_CSS, ERROR_CSS, LOGO_SVG,
	                   status, htmlEscape(title), htmlEscape(detail));
}

// ── file listing ─────────────────────────────────────────────────────────

inline constexpr std::string_view LISTING_CSS = R"CSS(main{max-width:960px;margin:9vh auto 0;padding:26px 28px 22px}
header{display:flex;align-items:center;justify-content:space-between;gap:16px;flex-wrap:wrap}
.stats{color:var(--steel);font-size:13px}
.toolbar{margin:18px 0 10px;display:flex;justify-content:flex-end}
#q{width:260px;max-width:100%;background:rgba(10,22,38,.55);border:1px solid var(--line);
border-radius:10px;padding:8px 12px;color:var(--ice);outline:none;transition:border-color .15s}
#q:focus{border-color:rgba(56,189,248,.6)}
#q::placeholder{color:var(--steel)}
table{width:100%;border-collapse:collapse}
th,td{text-align:left;padding:9px 10px;border-bottom:1px solid var(--line);white-space:nowrap}
th{color:var(--steel);font-weight:500;font-size:12px;letter-spacing:.5px;text-transform:lowercase;
user-select:none}
th[data-key]{cursor:pointer}
th[data-key]:hover{color:var(--pale)}
th.asc::after{content:" \2191";color:var(--sky)}
th.desc::after{content:" \2193";color:var(--sky)}
td.c-name{max-width:340px;overflow:hidden;text-overflow:ellipsis}
.stem{color:var(--ice)}
.ext{color:var(--steel)}
td.c-size{text-align:right;font-variant-numeric:tabular-nums}
.s-sm{color:var(--aqua)}.s-md{color:var(--pale)}.s-lg{color:var(--sky)}
td.c-date{color:var(--steel);font-variant-numeric:tabular-nums}
td.c-actions{text-align:right}
td.c-actions .btn{margin-left:6px}
tr:hover td{background:rgba(56,189,248,.05)}
tr.empty td{color:var(--steel);text-align:center;padding:34px 0;font-style:italic})CSS";

inline constexpr std::string_view LISTING_JS = R"JS(<script>
(function () {
	var rows = Array.prototype.slice.call(document.querySelectorAll('#files tbody tr:not(.empty)'));
	var q = document.getElementById('q');
	var state = { key: 'date', dir: -1 };

	function applyFilter() {
		var s = q.value.trim().toLowerCase();
		rows.forEach(function (r) {
			r.style.display = !s || r.dataset.search.toLowerCase().indexOf(s) !== -1 ? '' : 'none';
		});
	}
	q.addEventListener('input', applyFilter);

	function sort() {
		var k = state.key, d = state.dir;
		rows.sort(function (a, b) {
			if (k === 'name') return a.dataset.name.localeCompare(b.dataset.name) * d;
			return (Number(a.dataset[k]) - Number(b.dataset[k])) * d;
		});
		var body = document.querySelector('#files tbody');
		rows.forEach(function (r) { body.appendChild(r); });
	}

	document.querySelectorAll('#files th[data-key]').forEach(function (th) {
		th.addEventListener('click', function () {
			var k = th.dataset.key;
			state.dir = (state.key === k) ? -state.dir : (k === 'name' ? 1 : -1);
			state.key = k;
			document.querySelectorAll('#files th').forEach(function (t) {
				t.classList.remove('asc', 'desc');
			});
			th.classList.add(state.dir === 1 ? 'asc' : 'desc');
			sort();
		});
	});

	document.querySelectorAll('button.copy').forEach(function (b) {
		b.addEventListener('click', function () {
			var hash = b.dataset.hash;
			var done = function () {
				var old = b.textContent;
				b.textContent = 'copied';
				setTimeout(function () { b.textContent = old; }, 1200);
			};
			if (navigator.clipboard && navigator.clipboard.writeText)
				navigator.clipboard.writeText(hash).then(done);
			else {
				var i = document.createElement('input');
				i.value = hash; document.body.appendChild(i); i.select();
				document.execCommand('copy'); document.body.removeChild(i); done();
			}
		});
	});
})();
</script>)JS";

// one table row: name, hash, size bytes, date epoch, search text,
// stem, ext, size class, size text, date text, url, url, hash
inline constexpr std::string_view LISTING_ROW = R"HTML(		<tr data-name="{}" data-hash="{}" data-size="{}" data-date="{}" data-search="{}">
			<td class="c-name"><span class="stem">{}</span><span class="ext">{}</span></td>
			<td class="c-size {}">{}</td>
			<td class="c-date">{}</td>
			<td class="c-actions"><a class="btn" href="{}?view=yes">view</a> <a class="btn primary" href="{}?view=no">download</a> <button class="btn ghost copy" data-hash="{}">copy hash</button></td>
		</tr>
)HTML";

inline constexpr std::string_view LISTING_EMPTY =
	R"HTML(		<tr class="empty"><td colspan="4">no files yet &mdash; upload something with hikup</td></tr>
)HTML";

inline constexpr std::string_view LISTING_PAGE = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>hikup &middot; files</title>
<link rel="icon" href="{}">
<style>{}
{}</style>
</head>
<body>
<main class="card">
	<header>
		<div class="wordmark">{}<b>hikup</b><span>file server</span></div>
		<div style="display:flex;align-items:center;gap:14px">
			<span class="stats">{}</span>
			<a class="btn ghost" href="/logout">sign out</a>
		</div>
	</header>
	<div class="toolbar"><input id="q" type="search" placeholder="search name or hash&hellip;" autofocus></div>
	<table id="files">
		<thead><tr>
			<th data-key="name">name</th>
			<th data-key="size" class="c-size">size</th>
			<th data-key="date">uploaded</th>
			<th></th>
		</tr></thead>
		<tbody>
{}
		</tbody>
	</table>
</main>
{}
</body>
</html>
)HTML";

inline std::string listingPage ( const std::vector<FileRow>& rows ) {
	uintmax_t total = 0;
	for ( const auto& r : rows )
		total += r.size;

	std::string body;
	for ( const auto& r : rows ) {
		const auto name  = htmlEscape(r.name);
		const auto hash  = htmlEscape(r.hash);
		const auto stem  = htmlEscape(r.name.substr(0, r.name.find_last_of('.')));   // may be whole name
		const auto dot   = r.name.find_last_of('.');
		const auto ext   = dot == std::string::npos ? "" : htmlEscape(r.name.substr(dot));
		const auto sizeClass = r.size < 1024ull * 1024 ? "s-sm"
			: r.size < 100ull * 1024 * 1024 ? "s-md" : "s-lg";
		const auto url = "/" + r.hash + urlEncode(r.urlExt);

		body += std::format(LISTING_ROW, name, hash, r.size, r.date,
		                    htmlEscape(r.name + " " + r.hash), stem, ext, sizeClass,
		                    humanSize(static_cast<double>(r.size)), formatDate(r.date),
		                    url, url, hash);
	}

	if ( rows.empty() )
		body = LISTING_EMPTY;

	return std::format(LISTING_PAGE, FAVICON, BASE_CSS, LISTING_CSS, LOGO_SVG,
	                   std::format("{} {} &middot; {} total", rows.size(),
	                               rows.size() == 1 ? "file" : "files",
	                               humanSize(static_cast<double>(total))),
	                   body, LISTING_JS);
}

} // namespace WebPages

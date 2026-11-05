#include "HTTPFileServer.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "utils.hpp"
#include "WebPages.hpp"
#include "../shared/utils.hpp"

// session state for the web UI (the theme vars live in the header)
namespace HTTPFileServerVars {
	// web sessions: random cookie token -> expiry (sliding)
	inline std::mutex _sessionMutex;
	inline std::map<std::string, std::time_t> _sessions;
	constexpr int SESSION_LIFETIME = 7 * 24 * 3600; // seconds
}

[[nodiscard]] std::thread
HTTPFileServer::run ( std::string authUser, std::string authPass, const std::string& address ) {
	HTTPFileServerVars::_authUser = std::move(authUser);
	HTTPFileServerVars::_authPass = std::move(authPass);
	_generateSymLinks();
	return std::thread{&HTTPFileServer::_run, this, address};
}

void HTTPFileServer::_run ( const std::string& address ) const {
	mg_mgr mgr{}; // Event manager
	mg_mgr_init(&mgr); // Initialize event manager


	// Setup listener
	mg_http_listen(&mgr, address.c_str(), _ev_handler, nullptr);

	MG_INFO(( "Listening on: %s", address.c_str() ));
	MG_INFO(( "Web root: %s", HTTPFileServerVars::_rootDir.c_str() ));

	// Event loop
	while ( !_turnOff )
		mg_mgr_poll(&mgr, 1000);

	MG_INFO(( "Exiting" ));

	// Cleanup
	mg_mgr_free(&mgr);
}

std::string HTTPFileServer::createSymlinkFor ( const std::filesystem::path& file ) {
	auto fileName = file.filename().string();
	fileName = fileName.substr(0, fileName.find('.'));
	std::ranges::replace(fileName, '<', '.');
	const auto dotPos = fileName.find_last_of('.');
	if ( dotPos == std::string::npos )
		fileName = ".noext";
	else
		fileName = fileName.substr(dotPos);

	fileName = file.extension().string().substr(1) + fileName;

	const auto linkPath = std::filesystem::current_path() / "links" / fileName;

	if ( std::filesystem::exists(linkPath) )
		return fileName;

	std::filesystem::create_symlink(file, linkPath);
	return fileName;
}

void HTTPFileServer::removeSymlinkFor ( const std::filesystem::path& file ) {
	auto fileName = file.filename().string();
	fileName = fileName.substr(0, fileName.find('.'));
	std::ranges::replace(fileName, '<', '.');
	const auto dotPos = fileName.find_last_of('.');
	if ( dotPos == std::string::npos )
		fileName = ".noext";
	else
		fileName = fileName.substr(dotPos);

	fileName = file.extension().string().substr(1) + fileName;

	const auto linkPath = std::filesystem::current_path() / "links" / fileName;

	std::filesystem::remove(linkPath);
}

void HTTPFileServer::_generateSymLinks () {
	for ( const auto& file: std::filesystem::directory_iterator("storage") ) {
		auto fileName = file.path().filename().string();
		fileName = fileName.substr(0, fileName.find('.'));
		std::ranges::replace(fileName, '<', '.');
		const auto dotPos = fileName.find_last_of('.');
		if ( dotPos == std::string::npos )
			fileName = ".noext";
		else
			fileName = fileName.substr(dotPos);

		fileName = file.path().extension().string().substr(1) + fileName;

		const auto linkPath = std::filesystem::current_path() / "links" / fileName;

		if ( std::filesystem::exists(linkPath) )
			continue;

		std::filesystem::create_symlink(std::filesystem::current_path() / file, linkPath);
	}
}

// ── auth + sessions ───────────────────────────────────────────────────────

static bool checkCreds ( const std::string& user, const std::string& pass ) {
	return user == HTTPFileServerVars::_authUser && pass == HTTPFileServerVars::_authPass;
}

bool check_basic_auth ( mg_http_message* hm ) {
	char user[100], pass[100];
	// mg_http_creds parses the basic auth header and extracts username/password into buffers
	mg_http_creds(hm, user, sizeof( user ), pass, sizeof( pass ));

	return checkCreds(user, pass);
}

// credentials were sent (right or wrong)? — used to answer 401 instead of a
// login redirect for programmatic clients like curl
static bool has_basic_auth ( mg_http_message* hm ) {
	char user[100] = {0}, pass[100] = {0};
	mg_http_creds(hm, user, sizeof( user ), pass, sizeof( pass ));
	return user[0] != '\0' || pass[0] != '\0';
}

static std::string createSession () {
	unsigned char raw[24];
	mg_random(raw, sizeof raw);
	const auto token = bytesToHex(raw, sizeof raw);

	std::lock_guard lock(HTTPFileServerVars::_sessionMutex);
	HTTPFileServerVars::_sessions[token] = std::time(nullptr) + HTTPFileServerVars::SESSION_LIFETIME;
	return token;
}

static void destroySession ( const std::string& token ) {
	if ( token.empty() )
		return;

	std::lock_guard lock(HTTPFileServerVars::_sessionMutex);
	HTTPFileServerVars::_sessions.erase(token);
}

static std::string sessionTokenFrom ( mg_http_message* hm ) {
	const auto* cookie = mg_http_get_header(hm, "cookie");
	if ( cookie == nullptr )
		return {};

	const auto [buf, len] = mg_http_get_header_var(*cookie, mg_str("hikup_session"));
	return {buf, len};
}

static bool sessionValid ( const std::string& token ) {
	if ( token.empty() )
		return false;

	std::lock_guard lock(HTTPFileServerVars::_sessionMutex);
	auto& sessions = HTTPFileServerVars::_sessions;

	const auto now = std::time(nullptr);
	for ( auto it = sessions.begin(); it != sessions.end(); ) // purge expired
		if ( it->second < now )
			it = sessions.erase(it);
		else
			++it;

	const auto it = sessions.find(token);
	if ( it == sessions.end() )
		return false;

	it->second = now + HTTPFileServerVars::SESSION_LIFETIME; // slide
	return true;
}

static bool authorized ( mg_http_message* hm ) { return sessionValid(sessionTokenFrom(hm)) || check_basic_auth(hm); }

// ── pages (the HTML templates live in WebPages.hpp) ───────────────────────

static void serveLogin ( mg_connection* c, const bool failed ) {
	mg_http_reply(c, failed ? 401 : 200, "Content-Type: text/html\r\nCache-Control: no-store\r\n", "%s",
	              WebPages::loginPage(failed).c_str());
}

static void serveError ( mg_connection* c, const int status, const std::string& title, const std::string& detail ) {
	mg_http_reply(c, status, "Content-Type: text/html\r\nCache-Control: no-store\r\n", "%s",
	              WebPages::errorPage(status, title, detail).c_str());
}

static void serveListing ( mg_connection* c ) {
	std::vector<WebPages::FileRow> rows;

	std::error_code ec;
	const auto storageDir = std::filesystem::path(HTTPFileServerVars::_rootDir).parent_path() / "storage";
	for ( const auto& entry: std::filesystem::directory_iterator(storageDir, ec) ) {
		if ( !entry.is_regular_file(ec) )
			continue;

		// storage names are "name<with><dots>.<hash>" — dots became '<'
		const auto rawName = entry.path().filename().string();
		const auto dot = rawName.find('.');
		if ( dot == std::string::npos || dot + 1 >= rawName.size() )
			continue;

		WebPages::FileRow row;
		row.hash = rawName.substr(dot + 1);
		row.name = rawName.substr(0, dot);
		std::ranges::replace(row.name, '<', '.');
		row.urlExt = ".noext";
		if ( const auto lastDot = row.name.find_last_of('.');
			lastDot != std::string::npos )
			row.urlExt = row.name.substr(lastDot);
		row.size = std::filesystem::file_size(entry.path(), ec);
		ec.clear();
		row.date = std::chrono::system_clock::to_time_t(
			std::chrono::file_clock::to_sys(std::filesystem::last_write_time(entry.path(), ec)));

		rows.push_back(std::move(row));
	}

	std::ranges::sort(rows, [] ( const WebPages::FileRow& a, const WebPages::FileRow& b ) { return a.date > b.date; });

	mg_http_reply(c, 200, "Content-Type: text/html\r\nCache-Control: no-store\r\n", "%s",
	              WebPages::listingPage(rows).c_str());
}

// ── request routing ───────────────────────────────────────────────────────

void HTTPFileServer::_ev_handler ( mg_connection* c, const int ev, void* ev_data ) {
	mg_http_serve_opts opts = {
		HTTPFileServerVars::_rootDir.c_str(),
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		false,
		false,
		true
	};

	if ( ev == MG_EV_HTTP_MSG ) {
		auto* hm = static_cast<struct mg_http_message*>(ev_data);

		const auto request = std::string(hm->uri.buf, hm->uri.len);
		const auto method = std::string(hm->method.buf, hm->method.len);
		MG_INFO(( "File path: %s", request.c_str() ));

		// ── web UI: sign-in page + file listing ───────────────────────────
		if ( request == "/" ) {
			if ( authorized(hm) )
				serveListing(c);
			else if ( has_basic_auth(hm) )
				serveError(c, 401, "wrong credentials", "the username or password is incorrect");
			else
				mg_http_reply(c, 302, "Location: /login\r\n", "");
			return;
		}

		if ( request == "/login" ) {
			if ( method == "POST" ) {
				char user[100] = {0}, pass[100] = {0};
				mg_http_get_var(&hm->body, "user", user, sizeof user);
				mg_http_get_var(&hm->body, "pass", pass, sizeof pass);

				if ( checkCreds(user, pass) ) {
					const auto token = createSession();
					mg_http_reply(c, 302,
					              ( "Set-Cookie: hikup_session=" + token + "; Path=/; HttpOnly; SameSite=Lax; Max-Age="
					                + std::to_string(HTTPFileServerVars::SESSION_LIFETIME) + "\r\nLocation: /\r\n" ).
					              c_str(), "");
				}
				else { serveLogin(c, true); }
			}
			else if ( authorized(hm) ) { mg_http_reply(c, 302, "Location: /\r\n", ""); }
			else { serveLogin(c, false); }
			return;
		}

		if ( request == "/logout" ) {
			destroySession(sessionTokenFrom(hm));
			mg_http_reply(c, 302,
			              "Set-Cookie: hikup_session=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0\r\n"
			              "Location: /login\r\n", "");
			return;
		}

		// ── per-file serving (no auth) — links must keep working as-is ────

		// get hash from file name
		const auto hash = request.substr(1, request.find_last_of('.') - 1);
		auto fileName = Utils::FS::findCorrespondingFileName(hash).value_or("<<<<INVALID>>>>");
		MG_INFO(( "File path2: %s", fileName.c_str() ));
		fileName = fileName.substr(0, fileName.find('.'));
		std::ranges::replace(fileName, '<', '.');

		std::string ext;

		try { ext = fileName.substr(fileName.find_last_of('.')); }
		catch ( ... ) { ext = ".noext"; }

		const auto filePath = "/" + hash + ext;
		MG_INFO(( "File path3: %s", filePath.c_str() ));

		if ( !std::filesystem::exists(HTTPFileServerVars::_rootDir + filePath) ) {
			serveError(c, 404, "file not found", "no file matches this link — it may have been removed");
			return;
		}

		char buf[4] = {0};
		if ( mg_http_get_var(&hm->query, "view", buf, sizeof( buf )) <= 0 ) {
			// Default to download if 'view' parameter is not present
			strcpy(buf, HTTPFileServerVars::_httpDisplayInBrowser.c_str());
		}

		const auto path = HTTPFileServerVars::_rootDir + filePath;

		if ( strcmp(buf, "yes") == 0 ) {
			const auto header = "Content-Disposition: filename=\"" + fileName + "\"\r\n";
			opts.extra_headers = header.c_str();
			MG_INFO(( "Serving file: %s", path.c_str() ));
			mg_http_serve_file(c, hm, path.c_str(), &opts);
			return;
		}
		if ( strcmp(buf, "no") == 0 ) {
			const auto download_header = std::string("Content-Disposition: attachment; filename=\"") + fileName +
			                             "\"\r\n";
			opts.extra_headers = download_header.c_str();
			MG_INFO(( "Serving file: %s", path.c_str() ));
			mg_http_serve_file(c, hm, path.c_str(), &opts);
			return;
		}


		serveError(c, 404, "file not found", "unknown view parameter");
	}
}

#include "HTTPFileServer.hpp"

#include <algorithm>
#include <filesystem>

[[nodiscard]] std::thread HTTPFileServer::run ( std::string authUser, std::string authPass ) {
	HTTPFileServerVars::_authUser = std::move(authUser);
	HTTPFileServerVars::_authPass = std::move(authPass);
	_generateSymLinks();
	return std::thread{&HTTPFileServer::_run, this};
}

void HTTPFileServer::_run () const {
	mg_mgr mgr{}; // Event manager
	mg_mgr_init(&mgr); // Initialize event manager

	const auto addr = "http://0.0.0.0:6997";

	// Setup listener
	mg_http_listen(&mgr, addr, _ev_handler, nullptr);

	MG_INFO(("Listening on: %s", addr));
	MG_INFO(("Web root: %s", HTTPFileServerVars::_rootDir.c_str()));

	// Event loop
	while ( !_turnOff )
		mg_mgr_poll(&mgr, 1000);

	MG_INFO(("Exiting"));

	// Cleanup
	mg_mgr_free(&mgr);
}

void HTTPFileServer::_generateSymLinks () {
	for ( const auto& file: std::filesystem::directory_iterator("storage") ) {
		auto fileName = file.path().filename().string();
		fileName = fileName.substr(0, fileName.find('.'));
		std::ranges::replace(fileName, '<', '.');
		const auto linkPath = std::filesystem::current_path() / "links" / fileName;

		if ( std::filesystem::exists(linkPath) )
			continue;

		std::filesystem::create_symlink(std::filesystem::current_path() / file, linkPath);
	}
}

bool check_basic_auth ( mg_http_message* hm ) {
	char user[100], pass[100];
	// mg_http_get_basic_auth parses basic auth header and extracts username/password into buffers
	mg_http_creds(hm, user, sizeof( user ), pass, sizeof( pass ));

	return strcmp(user, HTTPFileServerVars::_authUser.c_str()) == 0 &&
		strcmp(pass, HTTPFileServerVars::_authPass.c_str()) == 0;
}

void HTTPFileServer::_ev_handler ( mg_connection* c, const int ev, void* ev_data ) {
	mg_http_serve_opts opts = {HTTPFileServerVars::_rootDir.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr};

	if ( ev == MG_EV_HTTP_MSG ) {
		auto* hm = static_cast<struct mg_http_message*>(ev_data);

		const auto filePath = std::string(hm->uri.buf, hm->uri.len);
		MG_INFO(("File path: %s", filePath.c_str()));

		if ( filePath == "/" ) {
			if ( !check_basic_auth(hm) ) {
				// Request authentication
				mg_http_reply(c, 401, "WWW-Authenticate: Basic realm=\"User Visible Realm\"\r\n", "Unauthorized\n");
				return;
			}
			mg_http_serve_dir(c, hm, &opts);
		}

		else if ( filePath.find("..") == std::string::npos ) {
			if ( !std::filesystem::exists(HTTPFileServerVars::_rootDir + filePath) ) {
				mg_http_reply(c, 404, "", "File not found");
				return;
			}

			const auto download_header = "Content-Disposition: attachment\r\n";
			opts.extra_headers = download_header;
			const auto path = HTTPFileServerVars::_rootDir + filePath;
			MG_INFO(("Serving file: %s", path.c_str()));
			mg_http_serve_file(c, hm, path.c_str(), &opts);
		}

		mg_http_reply(c, 404, "", "File not found");
	}
}

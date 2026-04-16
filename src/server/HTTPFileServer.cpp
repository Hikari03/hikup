#include "HTTPFileServer.hpp"

#include <algorithm>
#include <filesystem>

#include "utils.hpp"

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

std::string HTTPFileServer::createSymlinkFor( const std::filesystem::path & file ) {
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
		return "file already exists, this shouldn't happen";

	std::filesystem::create_symlink(file, linkPath);
	return fileName;
}

void HTTPFileServer::removeSymlinkFor( const std::filesystem::path & file ) {
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

bool check_basic_auth ( mg_http_message* hm ) {
	char user[100], pass[100];
	// mg_http_get_basic_auth parses basic auth header and extracts username/password into buffers
	mg_http_creds(hm, user, sizeof( user ), pass, sizeof( pass ));

	return strcmp(user, HTTPFileServerVars::_authUser.c_str()) == 0 && strcmp(
		       pass, HTTPFileServerVars::_authPass.c_str()) == 0;
}

void HTTPFileServer::_ev_handler ( mg_connection* c, const int ev, void* ev_data ) {
	mg_http_serve_opts opts = {HTTPFileServerVars::_rootDir.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr};

	if ( ev == MG_EV_HTTP_MSG ) {
		auto* hm = static_cast<struct mg_http_message*>(ev_data);

		const auto request = std::string(hm->uri.buf, hm->uri.len);
		MG_INFO(( "File path: %s", request.c_str() ));

		if ( request == "/" ) {
			if ( !check_basic_auth(hm) ) {
				// Request authentication
				mg_http_reply(c, 401, "WWW-Authenticate: Basic realm=\"User Visible Realm\"\r\n", "Unauthorized\n");
				return;
			}
			mg_http_serve_dir(c, hm, &opts);
		}


		// get hash from file name
		const auto hash = request.substr(1, request.find_last_of('.')-1);
		auto fileName = Utils::FS::findCorrespondingFileName(hash).value_or("<<<<INVALID>>>>");
		MG_INFO(( "File path2: %s", fileName.c_str() ));
		fileName = fileName.substr(0, fileName.find('.'));
		std::ranges::replace(fileName, '<', '.');
		const auto filePath = "/" + hash + fileName.substr(fileName.find_last_of('.'));
		MG_INFO(( "File path3: %s", filePath.c_str() ));

		if ( !std::filesystem::exists(HTTPFileServerVars::_rootDir + filePath) ) {
			mg_http_reply(c, 404, "", "File not found");
			return;
		}

		char buf[4] = {0};
		if ( mg_http_get_var(&hm->query, "view", buf, sizeof( buf )) <= 0 ) {
			// Default to download if 'view' parameter is not present
			strcpy(buf, "no");
		}

		const auto path = HTTPFileServerVars::_rootDir + filePath;

		if ( strcmp(buf, "yes") == 0 ) {
			const auto header = "Content-Disposition: filename=\"" + fileName+ "\"\r\n";
			opts.extra_headers = header.c_str();
			MG_INFO(( "Serving file: %s", path.c_str() ));
			mg_http_serve_file(c, hm, path.c_str(), &opts);
			return;
		}
		if ( strcmp(buf, "no") == 0 ) {
			const auto download_header = std::string("Content-Disposition: attachment; filename=\"") + fileName + "\"\r\n";
			opts.extra_headers = download_header.c_str();
			MG_INFO(( "Serving file: %s", path.c_str() ));
			mg_http_serve_file(c, hm, path.c_str(), &opts);
			return;
		}


		mg_http_reply(c, 404, "", "File not found");
	}
}
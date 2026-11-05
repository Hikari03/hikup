#pragma once

#include <filesystem>
#include <fstream>
#include <string>

#include "Style.hpp"
#include "../shared/Connection.hpp"

namespace CommandHandlers {

	// outcome of an upload: ok + stats on success, error set on failure
	struct UploadResult {
		bool ok = false;
		std::string error;
		double seconds = 0.0;    // wall time of the stream
		double speed = 0.0;      // average bytes/s
		std::string hash;        // hash confirmed by the server
		std::string httpLink;    // empty when the server has no HTTP frontend
	};

	UploadResult sendFile ( std::ifstream& file, const std::ifstream::pos_type fileSize, Connection& connection,
	                        bool quiet = false, const std::string& e2eKey = "", const ui::ProgressFn& progress = {} );
	std::string downloadFile ( Connection& connection, bool quiet = false,
	                           const std::filesystem::path& destination = {}, bool summary = true,
	                           const ui::ProgressFn& progress = {} );
	int downloadFileE2E ( Connection& connection, const std::string& key, bool quiet = false );
	int listFiles ( Connection& connection, const std::string& user, const std::string& pass, bool quiet = false );
	int infoFile ( Connection& connection, bool quiet = false );
	int fetchUrl ( Connection& connection, bool quiet = false );
}
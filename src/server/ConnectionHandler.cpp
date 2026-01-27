#include "ConnectionHandler.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <utility>

#include "utils.hpp"
#include "../shared/FileInfo.hpp"
#include "../shared/utils.hpp"

ConnectionHandler::ConnectionHandler ( Settings settings ) : settings(std::move(settings)) {}
void ConnectionHandler::addClient ( ClientInfo client ) {
    clientThreads.emplace_back( &ConnectionHandler::_serveConnection, this, std::move(client) );
}

void ConnectionHandler::_serveConnection ( ClientInfo client ) const {
    Utils::log("ConnectionHandler: serving client " + client.getIp());

    ConnectionServer connection(client);

    try {
        connection.init();
        const auto message = connection.receiveInternal();

        Utils::log("ConnectionHandler: received message: " + message );
        if ( message == "command:UPLOAD" )
            _receiveFile(connection);
        else if ( message == "command:DOWNLOAD" )
            _sendFile(connection);
        else if ( message == "command:REMOVE" )
            _removeFile(connection);
        else if ( message == "command:LIST" )
            _listFiles(connection);
    } catch ( const std::exception& e ) {
        Utils::elog("ConnectionHandler: error serving client: " + std::string(e.what()));
    }

}

void ConnectionHandler::_receiveFile ( ConnectionServer& connection ) const {
    const auto fileSize = stoll(connection.receiveInternal().substr(strlen("size:")));
	auto fileName = connection.receiveInternal().substr(strlen("filename:"));
	const auto hashFromClient = connection.receiveInternal().substr(strlen("hash:"));

	const auto oldFileName = fileName;

	const auto freeRam = std::min(getFreeMemory() / 4, static_cast<unsigned long>(fileSize / 16));

	connection.resizeBuffer(freeRam);

	// convert all '.' to '$' in the filename
	std::ranges::replace(fileName, '.', '<');

	std::filesystem::path _path = std::filesystem::current_path() / "storage" / ( fileName + '.' + hashFromClient );

	if ( std::filesystem::exists(_path) ) {
		Utils::log("receiveFile: file already exists");
		connection.sendInternal("NO");
		connection.sendInternal(settings.httpProtocol + "://" + settings.hostname + "/" + oldFileName);
		return;
	}

	connection.sendInternal("OK");

	std::ofstream file(_path, std::ios::binary);

	Utils::log("receiveFile: starting download of size: " + std::to_string(fileSize));

	std::string message;
	long long sizeWritten = 0;

	unsigned char hash[crypto_generichash_BYTES];
	crypto_generichash_state state;
	crypto_generichash_init(&state, nullptr, 0, sizeof hash);

	while ( true ) {
		try { message = connection.receive(); }
		catch ( const std::exception& e ) {
			std::cerr << "receiveFile: error receiving message: " << e.what() << std::endl;
			file.close();
			std::filesystem::remove(_path);
			connection.sendInternal("fail");
			return;
		}

		if ( message.starts_with(_internal"DONE") )
			break;

		file.write(message.data(), message.size());
		sizeWritten += message.size();

		connection.sendInternal("confirm");

		crypto_generichash_update(&state, reinterpret_cast<const unsigned char*>(message.data()), message.size());

		Utils::log( std::string("\r") + "main: " + humanReadableSize(sizeWritten) + " / " + humanReadableSize(fileSize) +
				" bytes written", false );
	}
	std::cout << std::endl;
	file.close();

	crypto_generichash_final(&state, hash, sizeof hash);

	auto hashString = binToHex(hash, sizeof hash);

	std::filesystem::create_symlink(_path, std::filesystem::current_path()/ "links" / oldFileName);

	connection.sendInternal(hashString);
	connection.sendInternal(std::to_string(settings.wantHttp));
	if ( settings.wantHttp )
		connection.sendInternal(settings.httpProtocol + "://" + settings.hostname + "/" + oldFileName);
}

void ConnectionHandler::_sendFile ( ConnectionServer& connection ) {
	auto hash = connection.receiveInternal().substr(strlen("hash:"));
	std::string fileName;

	for ( const auto& file: std::filesystem::directory_iterator("storage") )
		if ( file.path().extension() == '.' + hash )
			fileName = file.path();

	if ( fileName.empty() ) {
		Utils::log("sendFile: file not found");
		connection.sendInternal("NO");
		return;
	}

	std::ifstream file(fileName, std::ios::binary);

	if ( !file.good() ) {
		Utils::log("sendFile: could not open file");
		connection.sendInternal("NO");
		return;
	}

	const auto freeRam = getFreeMemory() / 4;

	connection.sendInternal("OK");

	const auto fileSize = std::filesystem::file_size(fileName);

	size_t chunkSize = 2 * 1024 * 1024;
	auto buffer = std::make_unique<char[]>(chunkSize);

	connection.sendInternal(std::to_string(fileSize));

	auto lastOfSlash = fileName.find_last_of('/');
	auto lastOfDot = fileName.find_last_of('.');

	auto clientFileName = fileName.substr(lastOfSlash+1, lastOfDot - lastOfSlash-1 );
	std::ranges::replace(clientFileName, '<', '.');

	connection.sendInternal(clientFileName);

	Utils::log("sendFile: starting upload of size: " + humanReadableSize(fileSize));

	size_t sizeRead = 0;

	while ( true ) {
		file.read(buffer.get(), chunkSize);

		const auto startUploadTime = std::chrono::high_resolution_clock::now();
		connection.send(std::string(buffer.get(), file.gcount()));
		const auto endUploadTime = std::chrono::high_resolution_clock::now();

		std::chrono::duration<double> duration = endUploadTime - startUploadTime;

		sizeRead += file.gcount();

		if ( connection.receiveInternal() != "confirm" )
			throw std::runtime_error("sendFile: client did not confirm the chunk");

		if ( sizeRead == static_cast<unsigned long long>(fileSize) )
			break;

		// adjust chunk size based on duration
		if (duration.count() > 1.4) {
			// decrease chunkSize by 2%, ensure integer rounding
			chunkSize = static_cast<int>(chunkSize * 0.75);

			buffer = std::make_unique<char[]>(chunkSize);
		}
		else if ( duration.count() < 0.3 && freeRam >= chunkSize * 2 ) {
			chunkSize = static_cast<int>(chunkSize * 2);
			buffer = std::make_unique<char[]>(chunkSize);

		}
		else if ( duration.count() < 0.6 && freeRam >= chunkSize * 2 ) {
			chunkSize = static_cast<int>(chunkSize * 1.25);
			buffer = std::make_unique<char[]>(chunkSize);
		}
	}

	connection.sendInternal("DONE");
}

void ConnectionHandler::_removeFile ( ConnectionServer& connection ) {
	const auto hash = connection.receiveInternal().substr(strlen("hash:"));

	std::filesystem::path fileName;

	for ( const auto& file: std::filesystem::directory_iterator("storage") )
		if ( file.path().extension() == '.' + hash )
			fileName = file.path();

	if ( fileName.empty() ) {
		Utils::log("removeFile: file not found: " + fileName.string());
		connection.sendInternal("NO");
		return;
	}

	auto symlinkName = fileName.filename().string().substr(0,fileName.filename().string().find('.'));
	std::ranges::replace(symlinkName, '<', '.');


	Utils::log( "removeFile: removing files: " + (std::filesystem::current_path() / "links" / symlinkName).string() + ", " + fileName.string());

	std::filesystem::remove(std::filesystem::current_path() / "links" / symlinkName);
	std::filesystem::remove(fileName);

	connection.sendInternal("OK");
}

void ConnectionHandler::_listFiles ( ConnectionServer& connection ) const {
	connection.sendInternal("OK");

	std::string user = connection.receiveInternal();

	if ( !user.starts_with("user:") )
		throw std::runtime_error("listFiles: no user specified, got: " + user);

	user = user.substr(strlen("user:") );

	std::string pass = connection.receiveInternal();

	if ( !pass.starts_with("pass:") )
		throw std::runtime_error("listFiles: no pass specified, got: " + pass);

	pass = pass.substr(strlen("user:"));

	if ( user != settings.authUser || pass != settings.authPass ) {
		Utils::log("Wrong Credentials: " + user + ", " + pass);
		connection.sendInternal("NOPE");
		return;
	}

	connection.sendInternal("OK");

	for ( const auto& file : std::filesystem::directory_iterator("storage") ) {
		connection.sendData(FileInfo(file, true).encode());
	}

	connection.sendInternal("DONE");

}

void ConnectionHandler::_cleanupClientThreads () {
    while (!stopRequested) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        std::erase_if(
            clientThreads,
            []( const std::jthread& t ) { return t.joinable(); }
        );
    }
}

#include "ConnectionHandler.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <set>
#include <utility>
#include <openssl/evp.h>

#include "HTTPFileServer.hpp"
#include "utils.hpp"
#include "../shared/FileInfo.hpp"
#include "../shared/utils.hpp"
#include "includes/toml.hpp"


ConnectionHandler::ConnectionHandler ( const Settings& settings )
	: _markedForRemoval("settings/toRemove.toml")
  , _readyFiles("settings/readyFiles.toml")
  , _settings(settings) {
	if ( !settings.syncTargets.empty() )
		_syncThread = std::jthread(&ConnectionHandler::_syncer, this);
}

void ConnectionHandler::addClient ( ClientInfo client ) {
	_clientThreads.emplace_back(&ConnectionHandler::_serveConnection, this, std::move(client));
}


void ConnectionHandler::_serveConnection ( ClientInfo client ) {
	Utils::log("ConnectionHandler: serving client " + client.getIp());

	ConnectionServer connection(client);

	try {
		connection.init();
		const auto message = connection.receiveInternal();

		Utils::log("ConnectionHandler: received message: " + message);
		if ( message == "command:UPLOAD" )
			_handleReceiveFile(connection);
		else if ( message == "command:DOWNLOAD" )
			_handleSendFile(connection);
		else if ( message == "command:REMOVE" )
			_handleRemoveFile(connection);
		else if ( message == "command:LIST" )
			_handleListFiles(connection);
		else if ( message == "command:SYNC" )
			_syncAsSlave(connection);
		else if ( message == "command:BATCH_UPLOAD")
			_handleBatchReceiveFile(connection);
		else if ( message == "command:BATCH_DOWNLOAD")
			_handleBatchSendFile(connection);
		else if ( message == "command:BATCH_REMOVE")
			_handleBatchRemoveFile(connection);
	}
	catch ( const std::exception& e ) {
		Utils::elog("ConnectionHandler: error serving client: " + std::string(e.what()));
	}
}

bool ConnectionHandler::_auth ( const std::string& user, const std::string& pass ) const {
	if ( !user.starts_with("user:") )
		throw std::runtime_error("listFiles: no user specified, got: " + user);

	const auto userCut = user.substr(strlen("user:"));

	if ( !pass.starts_with("pass:") )
		throw std::runtime_error("listFiles: no pass specified, got: " + pass);

	const auto passCut = pass.substr(strlen("user:"));

	if ( userCut != _settings.authUser || passCut != _settings.authPass ) {
		Utils::log("Wrong Credentials: " + userCut + ", " + passCut);
		return false;
	}

	return true;
}

template < ConnType T >
void ConnectionHandler::_handleReceiveFile ( T& connection ) {
	const auto fileSize = stoll(connection.receiveInternal().substr(strlen("size:")));
	auto fileName = connection.receiveInternal().substr(strlen("filename:"));
	const auto hashFromClient = connection.receiveInternal().substr(strlen("hash:"));

	const auto oldFileName = fileName;

	const auto freeRamLimit = 64 * 1024 * 1024; // 64 MB
	const auto freeRam = std::clamp(static_cast<unsigned long>(getFreeMemory() / 4), static_cast<unsigned long>(4 * 1024 * 1024), static_cast<unsigned long>(freeRamLimit));

	connection.resizeBuffer(freeRam);

	// convert all '.' to '<' in the filename
	std::ranges::replace(fileName, '.', '<');

	std::filesystem::path _path = std::filesystem::current_path() / "storage" / ( fileName + '.' + hashFromClient );

	if ( std::filesystem::exists(_path) ) {
		const auto HTTPLinkString = HTTPFileServer::createSymlinkFor(_path);
		connection.sendInternal("file already exists");
		connection.sendInternal(_settings.httpProtocol + "://" + _settings.hostname + "/" + HTTPLinkString);
		return;
	}

	connection.sendInternal("OK");

	_markedForRemoval.remove(hashFromClient);

	std::ofstream file(_path, std::ios::binary);
	
	Utils::log("receiveFile: starting download of size: " + std::to_string(fileSize));

	EVP_MD_CTX *mdctx;
	mdctx = EVP_MD_CTX_new();
	EVP_DigestInit_ex(mdctx, EVP_blake2s256(), nullptr);

    bool success = false;
    try {
        connection.receiveExact(fileSize, [&file, mdctx](const char* data, size_t size) {
            file.write(data, size);
            EVP_DigestUpdate(mdctx, data, size);
        });
        success = true;
    }
    catch ( const std::exception& e ) {
        std::cerr << "receiveFile: error receiving message: " << e.what() << std::endl;
        file.close();
        std::filesystem::remove(_path);
        EVP_MD_CTX_free(mdctx);
        return;
    }

	file.close();

    if (!success) {
        std::filesystem::remove(_path);
        EVP_MD_CTX_free(mdctx);
        return;
    }

	auto hash = std::make_unique<unsigned char[]>(EVP_MAX_MD_SIZE);
	unsigned int hashSize = EVP_MAX_MD_SIZE;

	EVP_DigestFinal_ex(mdctx, hash.get(), &hashSize);
	EVP_MD_CTX_free(mdctx);

	auto hashString = bytesToHex(hash.get(), hashSize);

	if ( hashFromClient != hashString ) {
		std::filesystem::remove(_path);
		Utils::elog(
			"Sent hash and calculated hash do not match:\n remote: " + hashFromClient + "\n local: " + hashString);
		_markedForRemoval.add(hashFromClient);
		connection.sendInternal("Sent hash and calculated hash do not match");
		return;
	}



	_readyFiles.add(hashString);

	if ( std::is_same_v<T, ConnectionServer> ) {
		connection.sendInternal("OK");
		auto HTTPLinkString = HTTPFileServer::createSymlinkFor(_path);

		connection.sendInternal(hashString);
		connection.sendInternal(std::to_string(_settings.wantHttp));
		if ( _settings.wantHttp && connection.receiveInternal() == "getHttpLink" )
			connection.sendInternal(_settings.httpProtocol + "://" + _settings.hostname + "/" + HTTPLinkString);
	}
}

void ConnectionHandler::_handleSendFile ( ConnectionServer& connection ) {
	auto hash = connection.receiveInternal().substr(strlen("hash:"));
	std::string fileName;

	for ( const auto& file: std::filesystem::directory_iterator("storage") )
		if ( file.path().extension() == '.' + hash )
			fileName = file.path();

	if ( fileName.empty() ) {
		Utils::log("sendFile: file not found");
		connection.sendInternal("File not found");
		return;
	}

    const auto fileSize = std::filesystem::file_size(fileName);

	if ( !_readyFiles.list().contains(hash) ) {
		Utils::log("sendFile: file is being uploaded");
		connection.sendInternal("File is being uploaded, try again later");
		return;
	}

	std::ifstream file(fileName, std::ios::binary);

	if ( !file.good() ) {
		Utils::log("sendFile: could not open file");
		connection.sendInternal("NO");
		return;
	}

	const auto freeRamLimit = 64 * 1024 * 1024; // 64 MB
	const auto freeRam = std::clamp(static_cast<unsigned long>(getFreeMemory() / 4), static_cast<unsigned long>(4 * 1024 * 1024), static_cast<unsigned long>(freeRamLimit));

	connection.sendInternal("OK");

	size_t chunkSize = 4 * 1024 * 1024;
	auto buffer = std::make_unique<char[]>(chunkSize);

	connection.sendInternal(std::to_string(fileSize));

	auto lastOfSlash = fileName.find_last_of('/');
	auto lastOfDot = fileName.find_last_of('.');

	auto clientFileName = fileName.substr(lastOfSlash + 1, lastOfDot - lastOfSlash - 1);
	std::ranges::replace(clientFileName, '<', '.');

	connection.sendInternal(clientFileName);

	Utils::log("sendFile: starting upload of size: " + humanReadableSize(fileSize));

	size_t sizeRead = 0;

	while ( true ) {
		file.read(buffer.get(), chunkSize);
        auto bytesRead = file.gcount();
        if (bytesRead == 0) break;

		const auto startUploadTime = std::chrono::high_resolution_clock::now();
		connection.sendRaw(buffer.get(), bytesRead);
		const auto endUploadTime = std::chrono::high_resolution_clock::now();

		std::chrono::duration<double> duration = endUploadTime - startUploadTime;

		sizeRead += bytesRead;

		if ( sizeRead == static_cast<unsigned long long>(fileSize) )
			break;

		// adjust chunk size based on duration
		if ( duration.count() > 1.0 ) {
			// decrease chunkSize by 25%
			chunkSize = static_cast<size_t>(chunkSize * 0.75);
			if (chunkSize < 1024 * 1024) chunkSize = 1024 * 1024;

			buffer = std::make_unique<char[]>(chunkSize);
		}
		else if ( duration.count() < 0.2 && freeRam >= chunkSize * 2 ) {
			chunkSize = static_cast<size_t>(chunkSize * 2);
			buffer = std::make_unique<char[]>(chunkSize);
		}
		else if ( duration.count() < 0.4 && freeRam >= chunkSize * 2 ) {
			chunkSize = static_cast<size_t>(chunkSize * 1.25);
			buffer = std::make_unique<char[]>(chunkSize);
		}
	}

	connection.receiveInternal();

}

void ConnectionHandler::_removeOnSyncedTargets ( const std::string& hash ) {
	_markedForRemoval.add(hash);

	for ( const auto& target: _settings.syncTargets ) {
		Connection connection;

		std::string address = target.targetAddress;
		int port = 6998;
		if ( size_t pos = address.find_last_of(':'); pos != std::string::npos ) {
			try {
				port = std::stoi(address.substr(pos + 1));
				address = address.substr(0, pos);
			} catch (...) {}
		}

		try { connection.connectToServer(target.targetAddress, port, target.tlsCertVerify); }
		catch ( ... ) {
			Utils::log("removeOnSyncedTargets: Could not connect to remote");
			continue;
		}

		Utils::log("removeOnSyncedTargets: Trying to remove file on \"" + target.targetName + "\": " + hash);

		connection.sendInternal("command:REMOVE").sendInternal("hash:" + hash);

		if ( connection.receiveInternal() == "OK" )
			Utils::log("removeOnSyncedTargets: Deletion successful");
		else { Utils::log("removeOnSyncedTargets: Deletion failed"); }
	}
}

void ConnectionHandler::_handleRemoveFile ( ConnectionServer& connection ) {
	const auto hash = connection.receiveInternal().substr(strlen("hash:"));

	std::filesystem::path fileName;

	for ( const auto& file: std::filesystem::directory_iterator("storage") )
		if ( file.path().extension() == '.' + hash )
			fileName = file.path();

	if ( fileName.empty() ) {
		Utils::log("_handleRemoveFile: file not found");
		connection.sendInternal("File not found");
		return;
	}

	if ( !_readyFiles.list().contains(hash) ) {
		Utils::log("_handleRemoveFile: file is being uploaded");
		connection.sendInternal("File is being uploaded, try again later");
		return;
	}
	connection.sendInternal("OK");
	_removeFile(fileName);

	_readyFiles.remove(hash);

	std::lock_guard lock(_syncMutex);
	_removeOnSyncedTargets(hash);
}

void ConnectionHandler::_handleListFiles ( ConnectionServer& connection ) const {
	connection.sendInternal("OK");

	const std::string user = connection.receiveInternal();
	const std::string pass = connection.receiveInternal();

	if ( !_auth(user, pass) ) {
		connection.sendInternal("NOPE");
		return;
	}

	connection.sendInternal("OK");

	for ( const auto& file: std::filesystem::directory_iterator("storage") ) {
		connection.sendData(FileInfo(file, true).encode());
	}

	connection.sendInternal("DONE");
}

void ConnectionHandler::_handleBatchReceiveFile ( ConnectionServer& connection ) {
	auto len = std::stoi(connection.receiveInternal().substr(strlen("length:")));

	while (len--) {
		_handleReceiveFile(connection);
	}
}

void ConnectionHandler::_handleBatchSendFile ( ConnectionServer& connection ) {
	auto len = std::stoi(connection.receiveInternal().substr(strlen("length:")));

	while (len--) {
		_handleSendFile(connection);
	}
}

void ConnectionHandler::_handleBatchRemoveFile ( ConnectionServer& connection ) {
	auto len = std::stoi(connection.receiveInternal().substr(strlen("length:")));

	while (len--) {
		_handleRemoveFile(connection);
	}
}


template < ConnType T >
void ConnectionHandler::_sendFileInSync ( T& connection, const std::string& fileName ) {
	const auto _path = std::filesystem::current_path() / "storage" / fileName;

	const auto fileSize = std::filesystem::file_size(_path);

	const auto lastOfSlash = fileName.find_last_of('/');
	const auto lastOfDot = fileName.find_last_of('.');
	auto clientStyleFileName = fileName.substr(lastOfSlash + 1, lastOfDot - lastOfSlash - 1);
	std::ranges::replace(clientStyleFileName, '<', '.');

	const auto hash = _path.extension().string().substr(1);

	connection.sendInternal("size:" + std::to_string(fileSize));
	connection.sendInternal("filename:" + clientStyleFileName);
	connection.sendInternal("hash:" + hash);

	if ( connection.receiveInternal() != "OK" ) {
		Utils::log("For some reason remote already has the file, skipping.");
		return;
	}

	std::ifstream file(_path, std::ios::binary);
	size_t chunkSize = 4 * 1024 * 1024;
	auto buffer = std::make_unique<char[]>(chunkSize);
	size_t sizeRead = 0;
	const auto freeRamLimit = 64 * 1024 * 1024; // 64 MB
	const auto freeRam = std::clamp(static_cast<unsigned long>(getFreeMemory() / 4), static_cast<unsigned long>(4 * 1024 * 1024), static_cast<unsigned long>(freeRamLimit));

	while ( true ) {
		file.read(buffer.get(), chunkSize);
        auto bytesRead = file.gcount();
        if (bytesRead == 0) break;

		const auto startUploadTime = std::chrono::high_resolution_clock::now();
		connection.sendRaw(buffer.get(), bytesRead);
		const auto endUploadTime = std::chrono::high_resolution_clock::now();

		std::chrono::duration<double> duration = endUploadTime - startUploadTime;

		sizeRead += bytesRead;

		if ( sizeRead == static_cast<unsigned long long>(fileSize) )
			break;

		// adjust chunk size based on duration
		if ( duration.count() > 1.0 ) {
			// decrease chunkSize by 25%
			chunkSize = static_cast<size_t>(chunkSize * 0.75);
			if (chunkSize < 1024 * 1024) chunkSize = 1024 * 1024;

			buffer = std::make_unique<char[]>(chunkSize);
		}
		else if ( duration.count() < 0.2 && freeRam >= chunkSize * 2 ) {
			chunkSize = static_cast<size_t>(chunkSize * 2);
			buffer = std::make_unique<char[]>(chunkSize);
		}
		else if ( duration.count() < 0.4 && freeRam >= chunkSize * 2 ) {
			chunkSize = static_cast<size_t>(chunkSize * 1.25);
			buffer = std::make_unique<char[]>(chunkSize);
		}
	}

}

// set substraction
std::set<std::string> operator/ ( const std::set<std::string>& set, const std::set<std::string>& rhs ) {
	std::set<std::string> result;

	for ( const auto& item: set ) {
		if ( !rhs.contains(item) )
			result.insert(item);
	}

	return result;
}

void ConnectionHandler::_syncAsSlave ( ConnectionServer& connection ) {
	const std::string user = connection.receiveInternal();
	const std::string pass = connection.receiveInternal();

	if ( !_auth(user, pass) ) {
		connection.sendInternal("Invalid credentials");
		return;
	}

	connection.sendInternal("OK");

	// ###################################### File removal
	{
		const auto remoteHashes = _parseHashes<std::set<std::string>>(connection.receiveData());
		const auto toRemove = Utils::FS::findCorrespondingFileNames(remoteHashes);
		const auto localHashes = _markedForRemoval.list();
		connection.sendData(_generateHashesString(localHashes));

		if ( !toRemove.empty() ) {
			Utils::log("ConnectionHandler::_syncAsSlave: removing " + std::to_string(toRemove.size()) + " files");

			for ( const auto& fileName: toRemove ) {
				Utils::log("ConnectionHandler: removing file " + fileName);
				_removeFile(std::filesystem::path("storage") / fileName);
			}
			_markedForRemoval.remove(remoteHashes);
		}
	}

	// ###################################### File exchange
	// Master sends array of hashes of his files in format hash|hash|...|
	const auto remoteHashes = _parseHashes<std::set<std::string>>(connection.receiveData());

	// Now we do the same
	const auto localHashes = _readyFiles.list();
	connection.sendData(_generateHashesString(localHashes));

	const auto toGet = remoteHashes / localHashes;
	const auto toSend = localHashes / remoteHashes;

	// Again, master is first to send missing files
	for ( size_t i = 0; i < toGet.size(); ++i ) {
		Utils::log(
			"ConnectionHandler::_syncAsSlave: getting file " + std::to_string(i + 1) + "/" + std::to_string(
				toGet.size()));
		_handleReceiveFile(connection);
	}

	const auto toSendFileNames = Utils::FS::findCorrespondingFileNames(toSend);

	for ( auto counter = 0; const auto& fileName: toSendFileNames ) {
		Utils::log(
			"ConnectionHandler::_syncAsSlave: sending file " + std::to_string(counter + 1) + "/" + std::to_string(
				toSend.size()));
		_sendFileInSync(connection, fileName);
		++counter;
	}

	Utils::log("Incoming sync complete");
}

void ConnectionHandler::_syncAsMaster ( const Settings::SyncTarget& target ) {
	// send command type and authenticate
	std::lock_guard lock(_syncMutex);
	Connection connection;

	std::string address = target.targetAddress;
	int port = 6998;
	if ( size_t pos = address.find_last_of(':'); pos != std::string::npos ) {
		try {
			port = std::stoi(address.substr(pos + 1));
			address = address.substr(0, pos);
		} catch (...) {}
	}

	connection.connectToServer(address, port, target.tlsCertVerify);

	connection.sendInternal("command:SYNC")
		.sendInternal("user:" + target.targetUser)
		.sendInternal("pass:" + target.targetPass);

	{
		if ( const auto response = connection.receiveInternal(); response != "OK" ) {
			throw std::runtime_error(response);
		}
	}

	// ###################################### File removal
	{
		const auto localHashes = _markedForRemoval.list();
		connection.sendData(_generateHashesString(localHashes));

		const auto remoteHashes = _parseHashes<std::set<std::string>>(connection.receiveData());
		const auto toRemove = Utils::FS::findCorrespondingFileNames(remoteHashes);

		if ( !toRemove.empty() ) {
			Utils::log("ConnectionHandler::_syncAsMaster: removing " + std::to_string(toRemove.size()) + " files");

			for ( const auto& fileName: toRemove ) {
				Utils::log("ConnectionHandler: removing file " + fileName);
				_removeFile(std::filesystem::path("storage") / fileName);
			}
			_markedForRemoval.remove(remoteHashes);
		}
	}


	// ###################################### File exchange
	const auto localHashes = _readyFiles.list();
	connection.sendData(_generateHashesString(localHashes));

	const auto remoteHashes = _parseHashes<std::set<std::string>>(connection.receiveData());

	const auto toGet = remoteHashes / localHashes;
	const auto toSend = localHashes / remoteHashes;

	const auto toSendFileNames = Utils::FS::findCorrespondingFileNames(toSend);

	for ( auto counter = 0; const auto& fileName: toSendFileNames ) {
		Utils::log(
			"ConnectionHandler::_syncAsMaster: sending file " + std::to_string(counter + 1) + "/" + std::to_string(
				toSend.size()));
		_sendFileInSync(connection, fileName);
		++counter;
	}

	for ( size_t i = 0; i < toGet.size(); ++i ) {
		Utils::log(
			"ConnectionHandler::_syncAsMaster: getting file " + std::to_string(i + 1) + "/" + std::to_string(
				toGet.size()));
		_handleReceiveFile(connection);
	}

	Utils::log("ConnectionHandler::_syncAsMaster: sync complete");
}


void ConnectionHandler::_syncer () {
	Utils::log("ConnectionHandler: syncing on");

	while ( !_stopRequested ) {
		for ( const auto& target: _settings.syncTargets ) {
			try { _syncAsMaster(target); }
			catch ( const std::exception& e ) {
				Utils::elog(
					"Error occurred when trying sync to \"" + target.targetName + "\" on address " + target.
					targetAddress + ": " + e.what());
			}
		}

		std::this_thread::sleep_for(std::chrono::seconds(_settings.syncPeriod));
	}
}

// Parses string in 'hash|hash|...|' format into an array
template < SetOrVectorOfString T >
T ConnectionHandler::_parseHashes ( const std::string& hashesString ) {
	T hashes;

	size_t offset = 0;

	while ( offset < hashesString.length() ) {
		const auto separator = hashesString.find_first_of('|', offset);

		if ( separator == std::string::npos )
			throw std::runtime_error("Parsing hashes: invalid separator");

		hashes.emplace(hashesString.substr(offset, separator - offset));
		offset = separator + 1;
	}


	return hashes;
}

template < SetOrVectorOfString T >
std::string ConnectionHandler::_generateHashesString ( const T& hashes ) {
	std::string result;

	for ( const auto& hash: hashes )
		result += hash + '|';

	return result;
}

void ConnectionHandler::_removeFile ( const std::filesystem::path& path ) {


	Utils::log("removeFile: removing files: " + path.string());

	HTTPFileServer::removeSymlinkFor(path);
	std::filesystem::remove(path);
}

void ConnectionHandler::_cleanupClientThreads () {
	while ( !_stopRequested ) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		std::erase_if(_clientThreads, [] ( const std::jthread& t ) { return t.joinable(); });
	}
}

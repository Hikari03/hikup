#include "BatchHandlers.hpp"

#include <algorithm>
#include <utility>

#include "CommandHandlers.hpp"

int Batch::autoResolve ( const Command::Type type, Connection & connection, const std::vector<std::string> & files ) {
	switch ( type ) {
		case Command::Type::BATCH_UPLOAD:
			return Batch::upload(files, connection);

		case Command::Type::BATCH_DOWNLOAD:
			return Batch::download(connection, files);

		case Command::Type::BATCH_REMOVE:
			return Batch::remove(connection, files);

		case Command::Type::UPLOAD:
		case Command::Type::DOWNLOAD:
		case Command::Type::REMOVE:
		case Command::Type::LIST:
		case Command::Type::BATCH:
		case Command::Type::INVALID:
		throw std::invalid_argument("Batch::autoResolve: invalid type");
	}

	std::unreachable();
}

int Batch::upload ( const std::vector<std::string>& files, Connection& connection ) {
	if ( !connection.isConnected() )
		throw std::invalid_argument("Batch::upload: trying to operate without active connection");

	if ( files.empty() )
		throw std::invalid_argument("Batch::upload: no files to operate on");

	connection.sendInternal("command:" + Command::toString(Command::Type::BATCH_UPLOAD))
				.sendInternal("length:" + std::to_string(files.size()));

	int fileNum = 0;

	for ( const auto & fileString : files ) {
		fileNum++;
		std::cout << colorize("Sending file " + std::to_string(fileNum) + '/' + std::to_string(files.size()), Color::YELLOW) << '\n';

		auto [file, fileSize, fileName] = resolveFile(fileString);

		const auto freeMem = getFreeMemory();
		auto toAllocate = std::min(freeMem / 4, static_cast<unsigned long>(fileSize / 16));
		if ( toAllocate < freeMem / 2 )
			toAllocate = std::min(freeMem, static_cast<unsigned long>(fileSize));


		std::cout << colorize("Computing hash by chunks of size: ", Color::GREEN) << colorize(
			humanReadableSize(toAllocate), Color::CYAN
		) << std::endl;
		auto hash = computeHash(file, toAllocate, fileSize);
		std::cout << colorize("Hash computed", Color::GREEN) << std::endl;

		connection.sendInternal("size:" + std::to_string(fileSize))
					.sendInternal("filename:" + fileName)
					.sendInternal("hash:" + hash);

		if ( const auto reason = connection.receiveInternal(); reason != "OK" ) {
			std::cout << colorize("Reason: " + reason + '\n', Color::RED) << colorize("Hash: ", Color::PURPLE) <<
					colorize(hash, Color::CYAN) << std::endl;
			const auto httpLink = connection.receiveInternal();
			std::cout << colorize("HTTP link: ", Color::PURPLE) << colorize(httpLink, Color::CYAN) << std::endl;
		}

		CommandHandlers::sendFile(file, fileSize, connection);
	}

	return 0;

}

int Batch::download ( Connection& connection, const std::vector<std::string>& files ) {
	if ( !connection.isConnected() )
		throw std::invalid_argument("Batch::upload: trying to operate without active connection");

	if ( files.empty() )
		throw std::invalid_argument("Batch::upload: no files to operate on");

	connection.sendInternal("command:" + Command::toString(Command::Type::BATCH_DOWNLOAD))
				.sendInternal("length:" + std::to_string(files.size()));

	int fileNum = 0;

	for ( const auto & fileString : files ) {
		fileNum++;
		std::cout << colorize("Downloading file " + std::to_string(fileNum) + '/' + std::to_string(files.size()), Color::YELLOW) << '\n';

		connection.sendInternal("hash:" + fileString);

		if ( const auto reason = connection.receiveInternal(); reason != "OK" ) {
			std::cerr << colorize("Server did not accept the request\n", Color::RED);


			std::cout << colorize("Reason: " + reason, Color::RED) << std::endl;
			return 1;
		}

		CommandHandlers::downloadFile(connection);
	}

	return 0;
}

int Batch::remove ( Connection& connection, const std::vector<std::string>& files ) {
	if ( !connection.isConnected() )
		throw std::invalid_argument("Batch::upload: trying to operate without active connection");

	if ( files.empty() )
		throw std::invalid_argument("Batch::upload: no files to operate on");

	connection.sendInternal("command:" + Command::toString(Command::Type::BATCH_REMOVE))
				.sendInternal("length:" + std::to_string(files.size()));

	int fileNum = 0;

	for ( const auto & fileString : files ) {
		fileNum++;
		std::cout << colorize("Removing file " + std::to_string(fileNum) + '/' + std::to_string(files.size()), Color::YELLOW) << '\n' ;

		connection.sendInternal("hash:" + fileString);

		if ( const auto reason = connection.receiveInternal(); reason != "OK" ) {
			std::cerr << colorize("Server did not accept the request\n", Color::RED);


			std::cout << colorize("Reason: " + reason, Color::RED) << std::endl;
			return 1;
		}
	}

	return 0;
}

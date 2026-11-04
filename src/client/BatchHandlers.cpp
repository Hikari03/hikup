#include "BatchHandlers.hpp"

#include <algorithm>
#include <utility>

#include "CommandHandlers.hpp"
#include "Style.hpp"

int Batch::autoResolve ( const std::set<Command::Type> & command, Connection & connection, const std::vector<std::string> & files, bool quiet ) {
	if ( !Command::isValid(command) )
		throw std::invalid_argument("Batch::autoResolve: invalid command.");

	if ( !command.contains(Command::Type::BATCH) )
		throw std::invalid_argument("Batch::autoResolve: batch command not present.");

	if ( command.contains(Command::Type::UPLOAD) )
		return upload(connection, files, quiet);

	if ( command.contains(Command::Type::DOWNLOAD) )
		return download(connection, files, quiet);

	if ( command.contains(Command::Type::REMOVE) )
		return remove(connection, files, quiet);

	throw std::invalid_argument("Batch::autoResolve: command does not contain needed types.");
}

int Batch::upload ( Connection& connection, const std::vector<std::string>& files, bool quiet ) {
	if ( !connection.isConnected() )
		throw std::invalid_argument("Batch::upload: trying to operate without active connection");

	if ( files.empty() )
		throw std::invalid_argument("Batch::upload: no files to operate on");

	connection.sendInternal("command:BATCH_UPLOAD")
			.sendInternal("length:" + std::to_string(files.size()));

	// one row per file, labelled by its file name
	std::vector<std::string> names;
	names.reserve(files.size());
	for ( const auto & fileString : files )
		names.push_back(std::filesystem::path(fileString).filename().string());

	struct Item {
		std::ifstream file;
		uintmax_t size{};
		std::string name;
		std::string hash;
		bool refused = false;
		std::string reason;
		std::string httpLink;
		CommandHandlers::UploadResult result;
	};
	std::vector<Item> items(files.size());

	// ── Phase 1: hash everything, one pacman-style block ──────────────────
	if ( !quiet )
		ui::step("hashing", std::to_string(files.size()) + " files");
	ui::ProgressBlock hashBlock(names, !quiet);

	for ( size_t i = 0; i < files.size(); ++i ) {
		if ( !quiet && !hashBlock.active() )
			ui::step("hashing", names[i]);

		auto [file, fileSize, fileName] = resolveFile(files[i]);
		items[i].file = std::move(file);
		items[i].size = fileSize;
		items[i].name = fileName;

		const bool live = hashBlock.active();
		items[i].hash = computeHash(items[i].file, static_cast<size_t>(fileSize), quiet,
			live ? ui::ProgressFn{[&]( unsigned long long done, unsigned long long total, double speed ) {
				hashBlock.update(i, total ? double(done) / double(total) : 1.0,
				                 humanReadableSize(done) + "/" + humanReadableSize(total),
				                 humanReadableSpeed(speed),
				                 speed > 0 ? "eta " + ui::formatDuration(double(total - done) / speed) : "eta --");
			}} : ui::ProgressFn{});

		hashBlock.finish(i, "hashed");
		if ( !quiet && !hashBlock.active() )
			ui::done("hashed", names[i]);
	}

	// ── Phase 2: transfer everything, second pacman-style block ───────────
	if ( !quiet )
		ui::step("uploading", std::to_string(files.size()) + " files");
	ui::ProgressBlock transferBlock(names, !quiet);

	for ( size_t i = 0; i < items.size(); ++i ) {
		auto & item = items[i];

		if ( !quiet && !transferBlock.active() )
			ui::step("sending file", std::to_string(i + 1) + '/' + std::to_string(items.size()) + ' ' + names[i]);

		connection.sendInternal("size:" + std::to_string(item.size))
				.sendInternal("filename:" + item.name)
				.sendInternal("hash:" + item.hash);

		if ( const auto reason = connection.receiveInternal(); reason != "OK" ) {
			// the server already has this file and answers with its link; it
			// reads no data for it, so skip straight to the next file
			item.refused  = true;
			item.reason   = reason;
			item.httpLink = connection.receiveInternal() + files[i].substr(files[i].find_last_of('.'));

			transferBlock.finish(i, "already known", ui::GLASS_ROSE);
			if ( quiet ) {
				std::cout << item.httpLink << '\n';
			} else if ( !transferBlock.active() ) {
				ui::fail("server refused the upload: " + item.reason);
				ui::printTree(ui::paint("already known to the server", ui::FROSTED_STEEL), {
					{ "hash", item.hash, ui::AERO_SKY },
					{ "link", item.httpLink, ui::LUMINOUS_GLASS },
				});
			}
			continue;
		}

		const bool live = transferBlock.active();
		item.result = CommandHandlers::sendFile(item.file, item.size, connection, quiet, "",
			live ? ui::ProgressFn{[&]( unsigned long long done, unsigned long long total, double speed ) {
				transferBlock.update(i, total ? double(done) / double(total) : 1.0,
				                     humanReadableSize(done) + "/" + humanReadableSize(total),
				                     humanReadableSpeed(speed),
				                     speed > 0 ? "eta " + ui::formatDuration(double(total - done) / speed) : "eta --");
			}} : ui::ProgressFn{});

		transferBlock.finish(i, item.result.ok ? "done" : "failed",
		                     item.result.ok ? ui::AQUA_GLASS : ui::GLASS_ROSE);
	}

	// ── Summary: per-file results, printed once the block has closed ──────
	if ( !quiet && transferBlock.active() ) {
		for ( size_t i = 0; i < items.size(); ++i ) {
			const auto & item = items[i];

			if ( item.refused ) {
				ui::fail("server refused the upload: " + item.reason);
				ui::printTree(ui::paint("already known to the server", ui::FROSTED_STEEL), {
					{ "hash", item.hash, ui::AERO_SKY },
					{ "link", item.httpLink, ui::LUMINOUS_GLASS },
				});
			} else if ( item.result.ok ) {
				ui::printTree(ui::tag("+", ui::AQUA_GLASS) + " " + ui::paint("uploaded", ui::BOLD) + " "
				              + ui::styledName(item.name) + " "
				              + ui::paint(humanReadableSize(static_cast<size_t>(item.size)), ui::LUMINOUS_GLASS), {
					{ "time", ui::formatDuration(item.result.seconds) + " @ " + humanReadableSpeed(item.result.speed), ui::ICE_WHITE },
					{ "hash", item.hash, ui::AERO_SKY },
					{ "link", item.result.httpLink.empty() ? "not available" : item.result.httpLink,
					  item.result.httpLink.empty() ? ui::FROSTED_STEEL : ui::LUMINOUS_GLASS },
				});
			} else {
				ui::fail(names[i] + ": " + (item.result.error.empty() ? "upload failed" : item.result.error));
			}
		}
	}

	return 0;
}

int Batch::download ( Connection& connection, const std::vector<std::string>& files, bool quiet ) {
	if ( !connection.isConnected() )
		throw std::invalid_argument("Batch::upload: trying to operate without active connection");

	if ( files.empty() )
		throw std::invalid_argument("Batch::upload: no files to operate on");

	connection.sendInternal("command:BATCH_DOWNLOAD")
			.sendInternal("length:" + std::to_string(files.size()));

	// rows are labelled by a hash prefix — the real file name only arrives
	// with the download itself and then finishes the row
	std::vector<std::string> labels;
	labels.reserve(files.size());
	for ( const auto & fileString : files )
		labels.push_back(fileString.size() > 12 ? fileString.substr(0, 12) + "…" : fileString);

	if ( !quiet )
		ui::step("downloading", std::to_string(files.size()) + " files");
	ui::ProgressBlock block(labels, !quiet);

	int failures = 0;
	std::vector<std::pair<size_t, std::string>> refused;

	for ( size_t i = 0; i < files.size(); ++i ) {
		if ( !quiet && !block.active() )
			ui::step("downloading", std::to_string(i + 1) + '/' + std::to_string(files.size()));

		connection.sendInternal("hash:" + files[i]);

		if ( const auto reason = connection.receiveInternal(); reason != "OK" ) {
			refused.emplace_back(i, reason);
			block.finish(i, "not found", ui::GLASS_ROSE);
			failures++;
			continue;
		}

		const bool live = block.active();
		const auto name = CommandHandlers::downloadFile(connection, quiet, {}, !live,
			live ? ui::ProgressFn{[&]( unsigned long long done, unsigned long long total, double speed ) {
				block.update(i, total ? double(done) / double(total) : 1.0,
				             humanReadableSize(done) + "/" + humanReadableSize(total),
				             humanReadableSpeed(speed),
				             speed > 0 ? "eta " + ui::formatDuration(double(total - done) / speed) : "eta --");
			}} : ui::ProgressFn{});

		if ( name.empty() ) {
			block.finish(i, "failed", ui::GLASS_ROSE);
			failures++;
		} else {
			block.finish(i, name);
		}
	}

	// refusals are reported once the block has closed
	for ( const auto & [i, reason] : refused )
		ui::fail(labels[i] + "…: server did not accept the request: " + reason);

	return failures ? 1 : 0;
}

int Batch::remove ( Connection& connection, const std::vector<std::string>& files, bool quiet ) {
	if ( !connection.isConnected() )
		throw std::invalid_argument("Batch::upload: trying to operate without active connection");

	if ( files.empty() )
		throw std::invalid_argument("Batch::upload: no files to operate on");

	connection.sendInternal("command:BATCH_REMOVE")
			.sendInternal("length:" + std::to_string(files.size()));

	int fileNum = 0;

	for ( const auto & fileString : files ) {
		fileNum++;

		if ( !quiet ) {
			ui::step("removing", std::to_string(fileNum) + '/' + std::to_string(files.size()));
		}

		connection.sendInternal("hash:" + fileString);

		if ( const auto reason = connection.receiveInternal(); reason != "OK" ) {
			ui::fail("server did not accept the request: " + reason);
			continue;
		}

		if ( !quiet ) {
			ui::done("removed", fileString);
		}
	}

	return 0;
}

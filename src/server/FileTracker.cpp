#include "FileTracker.hpp"

#include "utils.hpp"

FileTracker::FileTracker ( const std::filesystem::path& path )
	: filePath(path) {
	std::ofstream out(path, std::ios_base::ate); // touch the file

	try { root = toml::parse_file(path.string()); }
	catch ( const toml::parse_error& err ) {
		Utils::elog(
			"Error parsing file '" + *err.source().path + "':\n" + std::string(err.description()) + "\n (" + err.
			source().begin + ")\n");
		throw std::runtime_error("Could not load array from " + path.string());
	}

	if ( !out ) { throw std::runtime_error("FileTracker::add: Cannot open file for writing"); }

	out.close();
}

void FileTracker::add ( const std::set<std::string>& additions ) {
	toml::array* arr = nullptr;

	// Check if the key "my_strings" exists and is an array
	if ( const auto val = root["array"]; val ) {
		if ( const auto existing_arr = val.as_array() ) {
			arr = existing_arr;
		}
		else throw std::runtime_error("FileTracker::add: \"array\" exists but is not an array");
	}
	else {
		// Create the array in root table
		root.insert("array", toml::array{});
		arr = root["array"].as_array();
	}

	if ( !arr )
		throw std::runtime_error("FileTracker::add: Failed to get or create the array");


	std::set<std::string> toAdd = additions;

	for ( const auto& item: *arr ) {
		if ( const auto hash = item.as_string() ) {
			if ( toAdd.contains(hash->get()) )
				toAdd.erase(hash->get());
		}
	}

	for ( const auto& addition: toAdd )
		arr->push_back(addition);

	// Serialize and save back
	std::ofstream out(filePath, std::ios::trunc);
	if ( !out ) { throw std::runtime_error("FileTracker::add: Cannot open file for writing"); }
	out << root;

	out.close();
}

void FileTracker::add ( const std::string& addition ) {
	toml::array* arr = nullptr;

	// Check if the key "my_strings" exists and is an array
	if ( const auto val = root["array"]; val ) {
		if ( const auto existing_arr = val.as_array() ) {
			arr = existing_arr;
		}
		else throw std::runtime_error("FileTracker::add: \"array\" exists but is not an array");
	}
	else {
		// Create the array in root table
		root.insert("array", toml::array{});
		arr = root["array"].as_array();
	}

	if ( !arr )
		throw std::runtime_error("FileTracker::add: Failed to get or create the array");

	for ( const auto& item: *arr ) {
		if ( const auto hash = item.as_string() ) {
			if ( hash->get() == addition )
				return;
		}
	}

	// Not found: append new {hash, count=1}

	arr->push_back(addition);

	// Serialize and save back
	std::ofstream out(filePath, std::ios::trunc);
	if ( !out )
		throw std::runtime_error("FileTracker::add: Cannot open file for writing");
	out << root;

	out.close();
}

void FileTracker::remove ( const std::set<std::string>& toRemove ) {
	if ( const auto val = root["array"]; val ) {
		if ( val.is_array() ) {
			std::vector<toml::const_array_iterator> itemsToRemove;

			const auto arr = val.as_array();
			for ( auto item = arr->begin(); item != arr->end(); ++item ) {
				if ( toRemove.contains(item->as_string()->get()) )
					itemsToRemove.push_back(item);
			}

			for ( const auto item: itemsToRemove )
				arr->erase(item);
		}
		else Utils::elog("FileTracker::remove: \"array\" exists but is not an array");
	}

	std::ofstream out(filePath);
	if ( !out )
		throw std::runtime_error("FileTracker::remove: Could not open file for writing: " + filePath.string());


	out << root;
}

void FileTracker::remove ( const std::string& toRemove ) {
	if ( const auto val = root["array"]; val ) {
		if ( val.is_array() ) {
			std::vector<toml::const_array_iterator> itemsToRemove;

			const auto arr = val.as_array();
			for ( auto item = arr->begin(); item != arr->end(); ++item ) {
				if ( item->as_string()->get() == toRemove )
					itemsToRemove.push_back(item);
			}

			for ( const auto item: itemsToRemove )
				arr->erase(item);
		}
		else Utils::elog("FileTracker::remove: \"array\" exists but is not an array");
	}

	std::ofstream out(filePath);
	if ( !out )
		throw std::runtime_error("FileTracker::remove: Could not open file for writing: " + filePath.string());


	out << root;
}

std::set<std::string> FileTracker::list () const {
	std::set<std::string> hashes;


	const toml::node* arrayNode = root.get("array");
	if ( !arrayNode ) {
		// No "array" key, return empty set
		return hashes;
	}

	const toml::array* arr = arrayNode->as_array();
	if ( !arr ) {
		Utils::elog("\"array\" key is not an array in TOML file: " + filePath.string());
		throw std::runtime_error("FileTracker::remove: \"array\" is not an array");
	}

	for ( const auto& item: *arr ) {
		if ( const auto& hash = item.as_string() )
			hashes.insert(hash->get());
		else Utils::elog("FileTracker::remove: One item in array array is not a string");
	}

	return hashes;
}

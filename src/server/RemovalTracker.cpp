#include "RemovalTracker.hpp"

#include "utils.hpp"

RemovalTracker::RemovalTracker( const std::filesystem::path& path ) : filePath(path) {

    std::ofstream {path};

    try { root = toml::parse_file(path.string()); }
    catch ( const toml::parse_error& err ) {
        Utils::elog(
            "Error parsing file '" + *err.source().path + "':\n" + std::string(err.description()) + "\n (" + err.
            source().begin + ")\n"
        );
        throw std::runtime_error(
            "Could not load toRemove from " + path.string()
        );
    }

    if ( root.empty() ) {
        root.insert("toRemove", toml::array{});
    }
}

bool RemovalTracker::add ( const std::set<std::string>& additions ) {
    toml::array* arr = nullptr;

    // Check if the key "my_strings" exists and is an array
    if ( const auto val = root["toRemove"]; val)
    {
        if ( const auto existing_arr = val.as_array())
        {
            arr = existing_arr;
        }
        else
        {
            Utils::elog("\"toRemove\" exists but is not an array");
            return false;
        }
    }
    else
    {
        // Create the array in root table
        root.insert("toRemove", toml::array{});
        arr = root["toRemove"].as_array();
    }

    if (!arr)
    {
        Utils::elog("Failed to get or create the array");
        return false;
    }

    std::set<std::string> toAdd = additions;

    for ( const auto& item : *arr )
    {
        if ( const auto hash = item.as_string() )
        {
            if ( toAdd.contains(hash->get()) )
                toAdd.erase(hash->get());
        }
    }

    for ( const auto& addition : toAdd )
        arr->push_back(addition);

    // Serialize and save back
    std::ofstream out(filePath, std::ios::trunc);
    if (!out)
    {
        Utils::elog("Cannot open file for writing");
        return false;
    }
    out << root;

    out.close();
    return true;
}

bool RemovalTracker::add ( const std::string& addition ) {
    toml::array* arr = nullptr;

        // Check if the key "my_strings" exists and is an array
        if ( const auto val = root["toRemove"]; val)
        {
            if ( const auto existing_arr = val.as_array())
            {
                arr = existing_arr;
            }
            else
            {
                Utils::elog("\"toRemove\" exists but is not an array");
                return false;
            }
        }
        else
        {
            // Create the array in root table
            root.insert("toRemove", toml::array{});
            arr = root["toRemove"].as_array();
        }

        if (!arr)
        {
            Utils::elog("Failed to get or create the array");
            return false;
        }

        for ( const auto& item : *arr )
        {
            if ( const auto hash = item.as_string() )
            {
                if ( hash->get() == addition )
                    return true;
            }
        }

        // Not found: append new {hash, count=1}

        arr->push_back(addition);

        // Serialize and save back
        std::ofstream out(filePath, std::ios::trunc);
        if (!out)
        {
            Utils::elog("Cannot open file for writing");
            return false;
        }
        out << root;

        out.close();
        return true;
}

void RemovalTracker::remove ( const std::set<std::string>& toRemove ) {
    if ( const auto val = root["toRemove"]; val )
    {
        if ( val.is_array() )
        {

            std::vector<toml::const_array_iterator> itemsToRemove;

            const auto arr = val.as_array();
            for ( auto item = arr->begin(); item != arr->end(); ++item ) {
                if ( toRemove.contains(item->as_string()->get()) ) {
                    itemsToRemove.push_back( item );
                }

            }

            for ( const auto item : itemsToRemove )
                arr->erase(item);
        }
        else
        {
            Utils::elog("\"toRemove\" exists but is not an array");
        }
    }

    std::ofstream out(filePath);
    if (!out) {
        throw std::runtime_error("Could not open file for writing: " + filePath.string());
    }

    out << root;
}

void RemovalTracker::remove ( const std::string& toRemove ) {

    if ( const auto val = root["toRemove"]; val )
    {
        if ( val.is_array() )
        {

            std::vector<toml::const_array_iterator> itemsToRemove;

            const auto arr = val.as_array();
            for ( auto item = arr->begin(); item != arr->end(); ++item ) {
                if ( item->as_string()->get() == toRemove ) {
                    itemsToRemove.push_back( item );
                }

            }

            for ( const auto item : itemsToRemove )
                arr->erase(item);
        }
        else
        {
            Utils::elog("\"toRemove\" exists but is not an array");
        }
    }

    std::ofstream out(filePath);
    if (!out) {
        throw std::runtime_error("Could not open file for writing: " + filePath.string());
    }

    out << root;
}

std::set<std::string> RemovalTracker::list () const {
    std::set<std::string> hashes;


    const toml::node* toRemoveNode = root.get("toRemove");
    if (!toRemoveNode)
    {
        // No "toRemove" key, return empty set
        return hashes;
    }

    const toml::array* arr = toRemoveNode->as_array();
    if (!arr)
    {
        Utils::elog("\"toRemove\" key is not an array in TOML file: " + filePath.string());
        throw std::runtime_error("\"toRemove\" is not an array");
    }

    for ( const auto& item : *arr )
    {
        if ( const auto& hash = item.as_string() )
        {
           hashes.insert(hash->get());
        }
        else
        {
            Utils::elog("One item in toRemove array is not a string");
            // optionally skip or throw; here skip.
        }
    }

    return hashes;
}
#pragma once


#include <set>
#include <thread>

#include "ClientInfo.hpp"
#include "ConnectionServer.hpp"
#include "FileTracker.hpp"
#include "Settings.hpp"
#include "../shared/Connection.hpp"
#include "includes/toml.hpp"

template < typename T >
concept ConnType = std::same_as<T, ConnectionServer> || std::same_as<T, Connection>;

template < typename T >
concept SetOrVectorOfString =
        std::same_as<T, std::vector<std::string>> ||
        std::same_as<T, std::set<std::string>>;

class ConnectionHandler {
public:
    explicit ConnectionHandler ( const Settings& settings );

    void addClient ( ClientInfo client );

    void requestStop () { _stopRequested = true; }

private:
    bool _stopRequested = false;
    std::jthread _syncThread;
    std::vector<std::jthread> _clientThreads;
    std::mutex _syncMutex;
    FileTracker _markedForRemoval;
	FileTracker _readyFiles;
    const Settings _settings;


    void _serveConnection ( ClientInfo client );

    [[nodiscard]] bool _auth ( const std::string& user, const std::string& pass ) const;

    template < ConnType T >
    void _handleReceiveFile ( T& connection );

    static void _handleSendFile ( ConnectionServer& connection );

    void _removeOnSyncedTargets ( const std::string& hash );

    void _handleRemoveFile ( ConnectionServer& connection );

    void _handleListFiles ( ConnectionServer& connection ) const;

    // internal

    template < ConnType T >
    static void _sendFileInSync ( T& connection, const std::string& fileName );

    template < SetOrVectorOfString T >
    T _findCorrespondingFileNames ( const std::set<std::string>& toFind ) const;

    void _syncAsSlave ( ConnectionServer& connection );
    void _syncAsMaster ( const Settings::SyncTarget& target );
    void _syncer ();

    template < SetOrVectorOfString T >
    static T _parseHashes ( const std::string& hashesString );

    template < SetOrVectorOfString T >
    static T _getLocalFileHashes ();

    template < SetOrVectorOfString T >
    static std::string _generateHashesString ( const T& hashes );

    static void _removeFile ( const std::filesystem::path& path );
    void _cleanupClientThreads ();
};

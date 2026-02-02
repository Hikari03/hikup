#pragma once


#include <set>
#include <thread>

#include "ClientInfo.hpp"
#include "ConnectionServer.hpp"
#include "Settings.hpp"
#include "../client/Connection.hpp"

template < typename T >
concept ConnType = std::same_as<T, ConnectionServer> || std::same_as<T, Connection>;

template < typename T >
concept SetOrVectorOfString =
        std::same_as<T, std::vector<std::string>> ||
        std::same_as<T, std::set<std::string>>;

class ConnectionHandler {
public:
    explicit ConnectionHandler ( Settings settings );

    void addClient ( ClientInfo client );

    void requestStop () { stopRequested = true; }

private:
    bool stopRequested = false;
    std::vector<std::jthread> clientThreads;
    Settings settings;
    std::jthread syncThread;


    void _serveConnection ( ClientInfo client ) const;

    [[nodiscard]] bool _auth ( const std::string& user, const std::string& pass ) const;

    void _receiveFile ( ConnectionServer& connection ) const;

    static void _sendFile ( ConnectionServer& connection );

    static void _removeFile ( ConnectionServer& connection );

    void _listFiles ( ConnectionServer& connection ) const;

    static void _sendFileInSync ( ConnectionServer& connection, const std::string& fileName );

    template < SetOrVectorOfString T >
    T _findCorrespondingFileNames ( const std::set<std::string>& toFind ) const;

    void _syncAsSlave ( ConnectionServer& connection ) const;
    void _syncer () const;

    template < SetOrVectorOfString T >
    static T _parseHashes ( const std::string& hashesString );

    template < SetOrVectorOfString T >
    static T _getLocalFileHashes ();

    template < SetOrVectorOfString T >
    static std::string _generateHashesString ( const T& hashes );


    void _cleanupClientThreads ();
};

#pragma once

#include <thread>

#include "ClientInfo.hpp"
#include "ConnectionServer.hpp"
#include "Settings.hpp"

class ConnectionHandler {
public:
    explicit ConnectionHandler ( Settings settings );

    void addClient ( ClientInfo client );

    void requestStop () { stopRequested = true; }

private:
    bool stopRequested = false;
    std::vector<std::jthread> clientThreads;
    Settings settings;

    void _serveConnection ( ClientInfo client ) const;

    void _receiveFile ( ConnectionServer& connection ) const;
    static void _sendFile ( ConnectionServer& connection );
    static void _removeFile ( ConnectionServer& connection );
    void _listFiles ( ConnectionServer& connection ) const;

    void _cleanupClientThreads ();
};

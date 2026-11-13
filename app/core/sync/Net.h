// Plain BSD-socket TCP for band sync (iOS, macOS, Linux). Non-blocking, polled from the message thread (no threads).
// Leader = Server (one listening socket, many followers). Follower = Client with auto-reconnect and a liveness timeout.
#pragma once
#include "Sync.h"
#include <string>
#include <vector>
#include <functional>

namespace wr::sync {

class Server {
public:
    ~Server() { close(); }
    bool listen (int port = kDefaultPort);          // 0 = any free port (tests); binds all interfaces
    int port() const { return port_; }
    void close();
    struct Events { std::function<void (int)> connected; std::function<void (int, const std::string&)> line; std::function<void (int)> disconnected; std::function<void (long long)> pinged; };
    void poll (double nowSec, const Events& ev);    // accept, read, keep-alive pings (1 s), drop silent followers (6 s)
    void send (int client, const std::string& line);
    void broadcast (const std::string& line);
    int clients() const { return (int) conns_.size(); }
    void kick (int client);                         // sends {"t":"kick"} and closes after it is flushed
    std::vector<std::string> clientNames() const;   // from hello
    std::string clientName (int id) const;
    std::string clientHost (int id) const;
    int clientId (const std::string& name) const;   // 0 if that device is not connected
private:
    struct Conn { int fd = -1, id = 0; LineBuffer in; std::string out, name, host; double lastRx = 0; bool closing = false; };
    void flush (Conn&); void drop (size_t i, const Events& ev);
    int fd_ = -1, port_ = 0, nextId_ = 1; long long pingId_ = 0; double lastPing_ = 0; std::vector<Conn> conns_;
};

class Client {
public:
    ~Client() { disconnect(); }
    void connectTo (const std::string& host, int port);   // keeps retrying until disconnect()
    void disconnect();                                    // user left the session
    struct Events { std::function<void()> connected; std::function<void (const std::string&)> line; std::function<void()> disconnected; };
    void poll (double nowSec, const Events& ev);
    void send (const std::string& line);
    bool connected() const { return state_ == Connected; }
    int attempts() const { return attempts_; }
    std::string host() const { return host_; }
    static constexpr double kSilenceSec = 4.0;            // leader pings every second
private:
    enum St { Idle, Waiting, Connecting, Connected } state_ = Idle;
    void startConnect (double now); void lost (double now, const Events& ev);
    std::string host_; int port_ = 0, fd_ = -1, attempts_ = 0; double retryAt_ = 0, lastRx_ = 0, connStart_ = 0; LineBuffer in_; std::string out_;
};

} // namespace wr::sync

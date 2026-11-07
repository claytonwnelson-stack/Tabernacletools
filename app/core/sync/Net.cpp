#include "Net.h"
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <algorithm>

#ifndef MSG_NOSIGNAL
 #define MSG_NOSIGNAL 0
#endif

namespace wr::sync {

static void nonBlocking (int fd) { fcntl (fd, F_SETFL, fcntl (fd, F_GETFL, 0) | O_NONBLOCK); int one = 1; setsockopt (fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
#ifdef SO_NOSIGPIPE
    setsockopt (fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
}
// read what is available; false = closed / error
static bool readAll (int fd, LineBuffer& in, const std::function<void (const std::string&)>& onLine) {
    char buf[8192];
    for (;;) {
        ssize_t n = ::recv (fd, buf, sizeof buf, 0);
        if (n > 0) { in.feed (buf, (size_t) n, onLine); continue; }
        if (n == 0) return false;
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
    }
}
static bool writeSome (int fd, std::string& out) {
    while (! out.empty()) {
        ssize_t n = ::send (fd, out.data(), out.size(), MSG_NOSIGNAL);
        if (n > 0) { out.erase (0, (size_t) n); continue; }
        return n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR);
    }
    return true;
}

// ---------------------------------------------------------------- server
bool Server::listen (int port) {
    close(); fd_ = ::socket (AF_INET, SOCK_STREAM, 0); if (fd_ < 0) return false;
    int one = 1; setsockopt (fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a {}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl (INADDR_ANY); a.sin_port = htons ((uint16_t) port);
    if (::bind (fd_, (sockaddr*) &a, sizeof a) != 0 || ::listen (fd_, 16) != 0) { close(); return false; }
    socklen_t l = sizeof a; getsockname (fd_, (sockaddr*) &a, &l); port_ = ntohs (a.sin_port); nonBlocking (fd_); return true;
}
void Server::close() { for (auto& c : conns_) ::close (c.fd); conns_.clear(); if (fd_ >= 0) ::close (fd_); fd_ = -1; port_ = 0; }
void Server::flush (Conn& c) { writeSome (c.fd, c.out); }
void Server::drop (size_t i, const Events& ev) { int id = conns_[i].id; ::close (conns_[i].fd); conns_.erase (conns_.begin() + (long) i); if (ev.disconnected) ev.disconnected (id); }
void Server::poll (double now, const Events& ev) {
    if (fd_ < 0) return;
    for (;;) { int c = ::accept (fd_, nullptr, nullptr); if (c < 0) break; nonBlocking (c); Conn k; k.fd = c; k.id = nextId_++; k.lastRx = now; conns_.push_back (std::move (k)); if (ev.connected) ev.connected (conns_.back().id); }
    if (now - lastPing_ >= 1.0) { lastPing_ = now; broadcast (encode ("ping", -1, Json::object().set ("id", (double) ++pingId_))); if (ev.pinged) ev.pinged (pingId_); }
    for (size_t i = 0; i < conns_.size();) {
        auto& c = conns_[i]; const int id = c.id; bool got = false;
        bool alive = readAll (c.fd, c.in, [&] (const std::string& l) { got = true;
            Msg m; if (decode (l, m) && m.type == "hello") conns_[i].name = m.body["name"].str();
            if (ev.line) ev.line (id, l); });
        // (ev.line may have queued replies on this connection; re-fetch by index - the vector is not modified inside)
        if (got) conns_[i].lastRx = now;
        if (alive) alive = writeSome (conns_[i].fd, conns_[i].out);
        if (! alive || now - conns_[i].lastRx > 6.0 || (conns_[i].closing && conns_[i].out.empty())) { drop (i, ev); continue; }
        ++i;
    }
}
void Server::send (int id, const std::string& line) { for (auto& c : conns_) if (c.id == id) { c.out += line; flush (c); } }
void Server::kick (int id) { for (auto& c : conns_) if (c.id == id) { c.out += encode ("kick", -1, Json::object()); flush (c); c.closing = true; } }
void Server::broadcast (const std::string& line) { for (auto& c : conns_) { c.out += line; flush (c); } }
std::vector<std::string> Server::clientNames() const { std::vector<std::string> n; for (auto& c : conns_) n.push_back (c.name); return n; }

// ---------------------------------------------------------------- client
void Client::connectTo (const std::string& host, int port) { disconnect(); host_ = host; port_ = port; state_ = Waiting; retryAt_ = 0; attempts_ = 0; }
void Client::disconnect() { if (fd_ >= 0) ::close (fd_); fd_ = -1; state_ = Idle; out_.clear(); in_.buf.clear(); }
void Client::startConnect (double now) {
    ++attempts_; addrinfo hints {}, *res = nullptr; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    const std::string p = std::to_string (port_);
    if (getaddrinfo (host_.c_str(), p.c_str(), &hints, &res) != 0 || ! res) { retryAt_ = now + std::min (3.0, 0.5 * attempts_); return; }
    fd_ = ::socket (AF_INET, SOCK_STREAM, 0); nonBlocking (fd_);
    int r = ::connect (fd_, res->ai_addr, res->ai_addrlen); freeaddrinfo (res);
    if (r != 0 && errno != EINPROGRESS) { ::close (fd_); fd_ = -1; retryAt_ = now + std::min (3.0, 0.5 * attempts_); return; }
    state_ = Connecting; connStart_ = now;
}
void Client::lost (double now, const Events& ev) {
    const bool was = state_ == Connected; if (fd_ >= 0) ::close (fd_); fd_ = -1; in_.buf.clear(); out_.clear();
    state_ = Waiting; retryAt_ = now + std::min (3.0, 0.5 * std::max (1, attempts_)); if (was && ev.disconnected) ev.disconnected();
}
void Client::poll (double now, const Events& ev) {
    if (state_ == Idle) return;
    if (state_ == Waiting) { if (now >= retryAt_) startConnect (now); if (state_ != Connecting) return; }
    if (state_ == Connecting) {
        int err = 0; socklen_t l = sizeof err; char probe;
        // writable test without select(): a zero-length send succeeds once connected
        if (::send (fd_, &probe, 0, MSG_NOSIGNAL) == 0 && getsockopt (fd_, SOL_SOCKET, SO_ERROR, &err, &l) == 0 && err == 0) {
            sockaddr_in peer {}; socklen_t pl = sizeof peer;
            if (getpeername (fd_, (sockaddr*) &peer, &pl) == 0) { state_ = Connected; attempts_ = 0; lastRx_ = now; if (ev.connected) ev.connected(); }
        }
        if (state_ == Connecting) { if ((err != 0 && err != EINPROGRESS) || now - connStart_ > 3.0) lost (now, ev); return; }
    }
    bool got = false;
    bool alive = readAll (fd_, in_, [&] (const std::string& line) { got = true; if (ev.line) ev.line (line); });
    if (state_ != Connected) return;          // disconnect() called from a callback
    if (got) lastRx_ = now;
    if (alive) alive = writeSome (fd_, out_);
    if (! alive || now - lastRx_ > kSilenceSec) lost (now, ev);
}
void Client::send (const std::string& line) { if (state_ != Connected) return; out_ += line; writeSome (fd_, out_); }

} // namespace wr::sync

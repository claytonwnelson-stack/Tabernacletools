// Bonjour / mDNS discovery for band sync: the leader advertises "_tabernacle._tcp" with its session name; followers
// browse and get a list to tap. Uses the dns_sd API (built into iOS/macOS; Avahi compat on Linux if installed).
// Works on an offline travel router: mDNS is link-local multicast, no internet or DNS server needed.
// Without dns_sd (e.g. this Linux box) available() is false and the UI offers "join by IP address" instead.
#pragma once
#include <string>
#include <vector>
#include <memory>

namespace wr::sync {

struct Peer { std::string name, host; int port = 0; };   // host: IPv4 dotted address once resolved

class Discovery {
public:
    Discovery(); ~Discovery();
    static bool available();
    bool advertise (const std::string& name, int port);  // leader
    void stopAdvertising();
    bool browse();                                        // follower: fills peers() as leaders appear / vanish
    void stopBrowsing();
    void poll();                                          // message thread, non-blocking
    std::vector<Peer> peers() const;                      // resolved leaders
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};

} // namespace wr::sync

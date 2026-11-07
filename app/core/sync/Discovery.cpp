#include "Discovery.h"
#include "Sync.h"

#if __has_include(<dns_sd.h>) && ! defined (WR_NO_BONJOUR)
 #include <dns_sd.h>
 #include <arpa/inet.h>
 #include <netinet/in.h>
 #include <sys/select.h>
 #include <map>
 #define WR_HAVE_DNSSD 1
#endif

namespace wr::sync {

#if WR_HAVE_DNSSD
struct Discovery::Impl {
    DNSServiceRef reg = nullptr, browser = nullptr;
    struct Svc { std::string name, domain; DNSServiceRef resolve = nullptr, addr = nullptr; Peer peer; bool ready = false; };
    std::map<std::string, Svc> svcs;   // by instance name

    static void pump (DNSServiceRef r) {
        if (! r) return; int fd = DNSServiceRefSockFD (r); if (fd < 0) return;
        for (int guard = 0; guard < 16; ++guard) {
            fd_set s; FD_ZERO (&s); FD_SET (fd, &s); timeval tv { 0, 0 };
            if (select (fd + 1, &s, nullptr, nullptr, &tv) <= 0) break;
            if (DNSServiceProcessResult (r) != kDNSServiceErr_NoError) break;
        }
    }
    static void DNSSD_API onAddr (DNSServiceRef, DNSServiceFlags, uint32_t, DNSServiceErrorType err, const char*, const struct sockaddr* a, uint32_t, void* ctx) {
        auto* s = (Svc*) ctx; if (err || ! a || a->sa_family != AF_INET) return;
        char buf[INET_ADDRSTRLEN] {}; inet_ntop (AF_INET, &((const sockaddr_in*) a)->sin_addr, buf, sizeof buf); s->peer.host = buf; s->ready = true;
    }
    static void DNSSD_API onResolve (DNSServiceRef, DNSServiceFlags, uint32_t iface, DNSServiceErrorType err, const char*, const char* host, uint16_t portNet, uint16_t, const unsigned char*, void* ctx) {
        auto* s = (Svc*) ctx; if (err) return; s->peer.port = ntohs (portNet);
        if (s->addr) DNSServiceRefDeallocate (s->addr), s->addr = nullptr;
        DNSServiceGetAddrInfo (&s->addr, 0, iface, kDNSServiceProtocol_IPv4, host, onAddr, s);
    }
    static void DNSSD_API onBrowse (DNSServiceRef, DNSServiceFlags flags, uint32_t iface, DNSServiceErrorType err, const char* name, const char* type, const char* domain, void* ctx) {
        auto* self = (Impl*) ctx; if (err) return;
        if (flags & kDNSServiceFlagsAdd) {
            auto& s = self->svcs[name]; s.name = name; s.domain = domain; s.peer.name = name;
            if (! s.resolve) DNSServiceResolve (&s.resolve, 0, iface, name, type, domain, onResolve, &s);
        } else { auto it = self->svcs.find (name); if (it != self->svcs.end()) { free (it->second); self->svcs.erase (it); } }
    }
    static void DNSSD_API onRegister (DNSServiceRef, DNSServiceFlags, DNSServiceErrorType, const char*, const char*, const char*, void*) {}
    static void free (Svc& s) { if (s.resolve) DNSServiceRefDeallocate (s.resolve); if (s.addr) DNSServiceRefDeallocate (s.addr); s.resolve = s.addr = nullptr; }
    ~Impl() { if (reg) DNSServiceRefDeallocate (reg); if (browser) DNSServiceRefDeallocate (browser); for (auto& kv : svcs) free (kv.second); }
};
bool Discovery::available() { return true; }
bool Discovery::advertise (const std::string& name, int port) {
    stopAdvertising();
    return DNSServiceRegister (&impl_->reg, 0, 0, name.c_str(), serviceType(), nullptr, nullptr, htons ((uint16_t) port), 0, nullptr, Impl::onRegister, impl_.get()) == kDNSServiceErr_NoError;
}
void Discovery::stopAdvertising() { if (impl_->reg) DNSServiceRefDeallocate (impl_->reg); impl_->reg = nullptr; }
bool Discovery::browse() {
    stopBrowsing();
    return DNSServiceBrowse (&impl_->browser, 0, 0, serviceType(), nullptr, Impl::onBrowse, impl_.get()) == kDNSServiceErr_NoError;
}
void Discovery::stopBrowsing() { if (impl_->browser) DNSServiceRefDeallocate (impl_->browser); impl_->browser = nullptr; for (auto& kv : impl_->svcs) Impl::free (kv.second); impl_->svcs.clear(); }
void Discovery::poll() { Impl::pump (impl_->reg); Impl::pump (impl_->browser); for (auto& kv : impl_->svcs) { Impl::pump (kv.second.resolve); Impl::pump (kv.second.addr); } }
std::vector<Peer> Discovery::peers() const { std::vector<Peer> v; for (auto& kv : impl_->svcs) if (kv.second.ready && kv.second.peer.port > 0) v.push_back (kv.second.peer); return v; }
#else
struct Discovery::Impl {};
bool Discovery::available() { return false; }
bool Discovery::advertise (const std::string&, int) { return false; }
void Discovery::stopAdvertising() {}
bool Discovery::browse() { return false; }
void Discovery::stopBrowsing() {}
void Discovery::poll() {}
std::vector<Peer> Discovery::peers() const { return {}; }
#endif
Discovery::Discovery() : impl_ (std::make_unique<Impl>()) {}
Discovery::~Discovery() = default;

} // namespace wr::sync

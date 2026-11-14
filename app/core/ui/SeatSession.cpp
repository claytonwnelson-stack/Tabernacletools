#include "SeatSession.h"
#include "CoreProcessor.h"
#include "Platform.h"
#include "sync/SeatKeys.h"

wr::SeatTransport SeatSession::urlTransport (juce::String base) {
    return [base] (const std::string& method, const std::string& path, const std::string& body, const std::string& bearer) {
        wr::HttpResponse r;
        juce::URL url (base + juce::String (path));
        if (! body.empty()) url = url.withPOSTData (juce::String (body));
        juce::String headers = "Content-Type: application/json\r\n"; if (! bearer.empty()) headers << "Authorization: Bearer " << juce::String (bearer) << "\r\n";
        int status = 0;
        auto opts = juce::URL::InputStreamOptions (body.empty() && method != "POST" ? juce::URL::ParameterHandling::inAddress : juce::URL::ParameterHandling::inPostData)
                        .withExtraHeaders (headers).withConnectionTimeoutMs (10000).withStatusCode (&status).withHttpRequestCmd (juce::String (method));
        if (auto in = url.createInputStream (opts)) { r.body = in->readEntireStreamAsString().toStdString(); r.status = status; }
        return r;   // status 0 = offline / DNS / TLS failure
    };
}
SeatSession::SeatSession (CoreProcessor& p) : proc_ (p), client_ (urlTransport (wr::kSeatServiceUrl), wr::kSeatPublicKeyHex, wr::kSeatAudience) {
    client_.parse (file().loadFileAsString().toStdString());
    // apply the cached seat immediately (works offline), then refresh in the background
    auto e = proc_.entitlements; client_.refresh (e, (int64_t) juce::Time::currentTimeMillis() / 1000, INT64_MAX);
    proc_.setEntitlements (e);
    refreshEntitlement();
}
SeatSession::~SeatSession() { alive_->store (false); pool_.removeAllJobs (true, 5000); }
juce::File SeatSession::file() const { return wr::platform::dataDirectory().getChildFile ("seat.json"); }
void SeatSession::save() { file().replaceWithText (juce::String (client_.serialise())); }
void SeatSession::async (std::function<wr::SeatResult (wr::SeatClient&)> work, std::function<void (wr::SeatResult)> done) {
    auto alive = alive_;
    pool_.addJob ([this, alive, work, done] {
        auto r = work (client_);
        juce::MessageManager::callAsync ([this, alive, r, done] { if (! alive->load()) return; save(); if (done) done (r); });
    });
}
void SeatSession::refreshEntitlement (bool force) {
    auto alive = alive_; auto e = std::make_shared<wr::Entitlements> (proc_.entitlements);   // copied on the message thread
    pool_.addJob ([this, alive, force, e] {
        client_.refresh (*e, (int64_t) juce::Time::currentTimeMillis() / 1000, force ? 0 : 12 * 3600);
        juce::MessageManager::callAsync ([this, alive, e] { if (! alive->load()) return; save(); proc_.setEntitlements (*e); });
    });
}

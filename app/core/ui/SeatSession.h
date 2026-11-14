// v0.11 church seats inside the app: owns wr::SeatClient with a juce::URL transport, runs calls off the message thread,
// persists session + token to <data>/seat.json, and pushes the seat into CoreProcessor::entitlements.
// Only constructed in App Store builds (WR_STORE_BUILD); sideload builds are already fully unlocked.
#pragma once
#include <JuceHeader.h>
#include "sync/SeatClient.h"

class CoreProcessor;
class SeatSession {
public:
    explicit SeatSession (CoreProcessor& p);
    ~SeatSession();
    wr::SeatClient& client() { return client_; }    // message thread only, between async calls
    // run `work` on the network thread, then `done` on the message thread
    void async (std::function<wr::SeatResult (wr::SeatClient&)> work, std::function<void (wr::SeatResult)> done);
    void refreshEntitlement (bool force = false);   // startup, foreground, every few hours
    void save();
    static wr::SeatTransport urlTransport (juce::String baseUrl);
private:
    CoreProcessor& proc_; wr::SeatClient client_; juce::ThreadPool pool_ { 1 };
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>> (true);
    juce::File file() const;
};

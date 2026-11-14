// Wi-Fi clock for band sync. No Ableton Link: a follower estimates how far its clock sits from the
// leader (NTP round trip) and slews its beat grid onto the leader's beat. Small errors move a fraction
// of a beat at a time. A tempo change uses a faster slew for a little over a second.
#pragma once
#include <algorithm>
#include <cmath>
#include <string>

namespace wr::sync {

struct NtpReading { double offsetSec = 0, rttSec = 0; bool ok = false; };

// leaderTime = followerTime + offset. t1 follower send, t2 leader receive, t3 leader send, t4 follower receive.
inline NtpReading ntpReading (double t1, double t2, double t3, double t4) {
    NtpReading r; r.rttSec = (t4 - t1) - (t3 - t2);
    if (! (r.rttSec >= 0.0) || r.rttSec > 0.75) return r;
    r.offsetSec = ((t2 - t1) + (t3 - t4)) * 0.5; r.ok = true; return r;
}

class ClockLock {
public:
    enum class Status { Off, Acquiring, Locked, Settling };
    void reset() { *this = {}; }
    bool observe (double t1, double t2, double t3, double t4, double leaderBeat, double bpm, double localNow) {
        auto n = ntpReading (t1, t2, t3, t4); if (! n.ok) return false;
        if (! (samples_ == 0 || n.rttSec <= bestRtt_ * 1.35)) return false;
        if (samples_ == 0) offset_ = n.offsetSec;
        else offset_ += (n.offsetSec - offset_) * (n.rttSec <= bestRtt_ * 1.05 ? 0.45 : 0.15);
        if (samples_ == 0 || n.rttSec < bestRtt_) bestRtt_ = n.rttSec; else bestRtt_ += (n.rttSec - bestRtt_) * 0.02;
        ++samples_; rtt_ = n.rttSec;
        const bool tempoChanged = std::abs (bpm - refBpm_) > 0.4 && samples_ > 1;
        refBeat_ = leaderBeat; refLeaderTime_ = t3; if (bpm > 0) refBpm_ = bpm;
        if (! slewed_) { b0_ = leaderBeatAt (localNow); t0_ = localNow; bpm_ = refBpm_; slewed_ = true; err_ = 0; status_ = Status::Acquiring; return true; }
        if (tempoChanged) quickUntil_ = localNow + 1.2;
        bpm_ = refBpm_; return true;
    }
    void steer (double localNow) {
        if (! slewed_ || samples_ == 0) { status_ = Status::Off; return; }
        const double target = leaderBeatAt (localNow);
        const double ours = b0_ + (localNow - t0_) * bpm_ / 60.0;
        err_ = target - ours;
        const bool quick = localNow < quickUntil_;
        if (std::abs (err_) > (quick ? 0.45 : 0.75)) { b0_ = target; t0_ = localNow; err_ = 0; }
        else { const double step = quick ? 0.012 : 0.0025; const double pull = std::clamp (err_, -step, step); b0_ = ours + pull; t0_ = localNow; err_ -= pull; }
        status_ = samples_ < 3 ? Status::Acquiring : std::abs (err_) < 0.02 ? Status::Locked : Status::Settling;
    }
    double beatAt (double localNow) const { return slewed_ ? b0_ + (localNow - t0_) * bpm_ / 60.0 : 0; }
    double leaderBeatAt (double localNow) const { return refBeat_ + ((localNow + offset_) - refLeaderTime_) * refBpm_ / 60.0; }
    double bpm() const { return bpm_ > 0 ? bpm_ : 120; }
    double audioBpm (double localNow) const {
        const double tau = localNow < quickUntil_ ? 0.35 : 1.6;
        return std::clamp (bpm() + std::clamp (err_ * 60.0 / tau, -6.0, 6.0), 30.0, 300.0);
    }
    Status status() const { return status_; }
    double errorBeats() const { return err_; }
    double rttMs() const { return rtt_ * 1000.0; }
    int samples() const { return samples_; }
    bool ready() const { return slewed_ && samples_ > 0; }
    bool engaged() const { return slewed_ && samples_ >= 3; }   // enough round trips to steer the click
    std::string label() const {
        if (status_ == Status::Locked) return "Clock locked";
        if (status_ == Status::Acquiring) return "Clock acquiring";
        if (status_ == Status::Settling) return "Clock settling";
        return "Clock off";
    }
private:
    double offset_ = 0, bestRtt_ = 1, rtt_ = 0, refBeat_ = 0, refLeaderTime_ = 0, refBpm_ = 120;
    double b0_ = 0, t0_ = 0, bpm_ = 120, err_ = 0, quickUntil_ = -1;
    int samples_ = 0; bool slewed_ = false; Status status_ = Status::Off;
};

} // namespace wr::sync

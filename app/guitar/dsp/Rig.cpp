#include "Oversampler.h"
#include "Rig.h"

namespace wr {

PedalSlot* Chain::find (const std::string& uid, Lane* laneOut, int* idxOut) {
    for (int l = 0; l < 2; ++l) for (size_t i = 0; i < lanes[l].size(); ++i)
        if (lanes[l][i].uid == uid) { if (laneOut) *laneOut = (Lane) l; if (idxOut) *idxOut = (int) i; return &lanes[l][i]; }
    return nullptr;
}

RigEngine::RigEngine() {}
RigEngine::~RigEngine() { delete tail_; delete active_; delete pending_.exchange (nullptr); delete retired_.exchange (nullptr); }

void RigEngine::prepare (double sr, int maxBlock) {
    fs_ = sr; maxBlock_ = std::max (16, maxBlock);
    for (auto* v : { &aL_, &aR_, &bL_, &bR_, &tL_, &tR_, &uL_, &uR_ }) v->assign ((size_t) maxBlock_, 0.0f);
    tracker_.prepare (sr); ctx_.sampleRate = sr; click.prepare (sr, maxBlock_);
    if (active_) for (auto& lane : active_->lanes) for (auto& s : lane) s.pedal->prepare (sr, maxBlock_);
}

void RigEngine::setChain (std::unique_ptr<Chain> c) {
    c->generation = ++gen_;
    const int f = oversamplingNow.load();
    for (auto& lane : c->lanes) for (auto& s : lane) { s.pedal->setOversampling (f); s.pedal->prepare (fs_, maxBlock_); }
    delete pending_.exchange (c.release()); // an unconsumed older pending chain is dropped
}

void RigEngine::collectGarbage() { delete retired_.exchange (nullptr); }

Command cmdEnable (const Chain& c, const std::string& uid, bool on) {
    Command m; m.type = Command::SetEnabled; m.generation = c.generation; m.value = on ? 1.0f : 0.0f;
    Lane l; int i; if (const_cast<Chain&> (c).find (uid, &l, &i)) { m.lane = (uint8_t) l; m.slot = (int16_t) i; }
    return m;
}
Command cmdParam (const Chain& c, const std::string& uid, const std::string& param, float v, bool* ok) {
    Command m; m.type = Command::SetParam; m.generation = c.generation; m.value = v;
    Lane l; int i; auto* s = const_cast<Chain&> (c).find (uid, &l, &i);
    if (s) { m.lane = (uint8_t) l; m.slot = (int16_t) i; m.param = (int16_t) s->pedal->paramIndex (param); }
    if (ok) *ok = s && m.param >= 0;
    return m;
}

void RigEngine::handle (const Command& c) {
    switch (c.type) {
        case Command::SetEnabled: case Command::SetParam: {
            if (! active_ || c.generation != active_->generation || c.slot < 0 || c.lane > 1) return;
            auto& lane = active_->lanes[c.lane]; if (c.slot >= (int) lane.size()) return;
            auto& p = *lane[(size_t) c.slot].pedal;
            if (c.type == Command::SetEnabled) p.setEnabled (c.value > 0.5f); else p.setParam (c.param, c.value);
            break; }
        case Command::SetBpm: bpm.store (clampf (c.value, 30, 300)); break;
        case Command::SetTrim: trim_[c.lane & 1] = std::pow (10.0f, clampf (c.value, -40, 12) / 20.0f); break;
        case Command::SetRouting: routing_ = (Routing) (int) c.value; break;
        case Command::SetTuner: tunerOn_ = c.value > 0.5f; break;
        case Command::SetTunerMute: tunerMute_ = c.value > 0.5f; break;
        case Command::SetOversampling: gov_.setUser ((int) c.value, c.param == 1); break;
        case Command::TailSec: tailSec_ = clampf (c.value, 0, 10); break;
        default: break;
        case Command::Panic: if (active_) for (auto& lane : active_->lanes) for (auto& s : lane) s.pedal->reset(); break;
    }
}

void RigEngine::runLane (std::vector<PedalSlot>& lane, float* L, float* R, int n) {
    for (auto& s : lane) if (! s.pedal->isSilent() || s.pedal->isEnabled()) s.pedal->process (L, R, n, ctx_);
}

void RigEngine::process (const float* const* ins, int numIns, float* const* outs, int numOuts, int total) {
    // pick up a new chain (structural change from the Workshop)
    if (auto* p = pending_.load (std::memory_order_acquire)) {
        if (retired_.load (std::memory_order_acquire) == nullptr) {
            pending_.store (nullptr);
            // graceful rig change: the outgoing chain keeps running on silence and fades out so delay/reverb tails ring
            if (tail_) { retired_.store (tail_); tail_ = nullptr; }
            if (active_) { tail_ = active_; tailGain_ = 1.0f; }
            active_ = p; applyOversampling (active_, osApplied_);
        }
    }
    Command c; while (queue_.pop (c)) handle (c);
    if (gov_.current != osApplied_) { osApplied_ = gov_.current; applyOversampling (active_, osApplied_); applyOversampling (tail_, osApplied_); }
    const auto t0 = std::chrono::steady_clock::now();
    click.drain();

    Routing r = routing_;
    if (r == Routing::FourCable && numOuts < 4) { r = Routing::TwoCable; fellBackToTwoCable.store (true); } else fellBackToTwoCable.store (false);
    routingNow.store ((int) r);
    const float* in0 = numIns > 0 ? ins[0] : nullptr; const float* in1 = numIns > 1 ? ins[1] : nullptr;

    for (int off = 0; off < total; off += maxBlock_) {
        const int n = std::min (maxBlock_, total - off);
        // tracker on raw guitar
        bool needPitch = tunerOn_;
        if (active_) for (auto& lane : active_->lanes) for (auto& s : lane) if (s.pedal->wantsPitch && s.pedal->isEnabled()) needPitch = true;
        ctx_.trackerRunning = needPitch; ctx_.bpm = bpm.load();
        if (needPitch && in0) { tracker_.push (in0 + off, n); ctx_.pitchHz = tracker_.frequency(); ctx_.pitchConf = tracker_.confidence(); ctx_.inputLevel = tracker_.level();
                                tunerHz.store (ctx_.pitchHz); tunerConf.store (ctx_.pitchConf); inLevel.store (ctx_.inputLevel); }

        for (int s = 0; s < n; ++s) { float x = in0 ? in0[off + s] : 0.0f; aL_[(size_t) s] = x; aR_[(size_t) s] = x; float y = (r == Routing::FourCable && in1) ? in1[off + s] : 0.0f; bL_[(size_t) s] = y; bR_[(size_t) s] = y; }
        if (active_) runLane (active_->lanes[0], aL_.data(), aR_.data(), n);
        if (r == Routing::TwoCable) { std::copy (aL_.begin(), aL_.begin() + n, bL_.begin()); std::copy (aR_.begin(), aR_.begin() + n, bR_.begin()); }
        if (active_) runLane (active_->lanes[1], bL_.data(), bR_.data(), n);
        if (tail_ && tailGain_ > 0) { // outgoing rig: silence in, faded tails out (same routing as the live chain)
            std::fill (tL_.begin(), tL_.begin() + n, 0.0f); std::fill (tR_.begin(), tR_.begin() + n, 0.0f); std::fill (uL_.begin(), uL_.begin() + n, 0.0f); std::fill (uR_.begin(), uR_.begin() + n, 0.0f);
            runLane (tail_->lanes[0], tL_.data(), tR_.data(), n);
            if (r == Routing::TwoCable) { std::copy (tL_.begin(), tL_.begin() + n, uL_.begin()); std::copy (tR_.begin(), tR_.begin() + n, uR_.begin()); }
            runLane (tail_->lanes[1], uL_.data(), uR_.data(), n);
            const float step = tailSec_ > 0 ? 1.0f / (tailSec_ * (float) fs_) : 1.0f;
            for (int s = 0; s < n; ++s) { float gg = tailGain_ * tailGain_; // ease-out
                if (r == Routing::FourCable) { aL_[(size_t) s] += tL_[(size_t) s] * gg; aR_[(size_t) s] += tR_[(size_t) s] * gg; }
                bL_[(size_t) s] += uL_[(size_t) s] * gg; bR_[(size_t) s] += uR_[(size_t) s] * gg; tailGain_ = std::max (0.0f, tailGain_ - step); }
        }
        if (tail_ && tailGain_ <= 0 && retired_.load (std::memory_order_acquire) == nullptr) { retired_.store (tail_); tail_ = nullptr; }
        tailRinging.store (tail_ != nullptr);

        const float mt = tunerMute_ ? 0.0f : 1.0f, mc = 1.0f - std::exp (-1.0f / (0.005f * (float) fs_)), tc = mc;
        const float rel = std::exp (-1.0f / (0.1f * (float) fs_));
        float pkA = 0, pkB = 0;
        auto lim = [&] (float x, int i) { float a = std::abs (x); limEnv_[i] = a > limEnv_[i] ? a : limEnv_[i] * rel; float g = limEnv_[i] > 0.95f ? 0.95f / limEnv_[i] : 1.0f; float y = x * g; return std::isfinite (y) ? clampf (y, -1, 1) : 0.0f; };
        for (int s = 0; s < n; ++s) {
            muteGain_ += (mt - muteGain_) * mc; trimSm_[0] += (trim_[0] - trimSm_[0]) * tc; trimSm_[1] += (trim_[1] - trimSm_[1]) * tc;
            float g0 = muteGain_ * trimSm_[0], g1 = muteGain_ * trimSm_[1];
            if (r == Routing::FourCable) {
                float pre = lim (0.5f * (aL_[(size_t) s] + aR_[(size_t) s]) * g0, 0);
                float pl = lim (bL_[(size_t) s] * g1, 1), pr = clampf (bR_[(size_t) s] * g1 * (limEnv_[1] > 0.95f ? 0.95f / limEnv_[1] : 1.0f), -1, 1);
                if (! std::isfinite (pr)) pr = 0;
                if (outs[0]) outs[0][off + s] = pre; if (numOuts > 1 && outs[1]) outs[1][off + s] = 0.0f;
                if (outs[2]) outs[2][off + s] = pl; if (outs[3]) outs[3][off + s] = pr;
                pkA = std::max (pkA, std::abs (pre)); pkB = std::max (pkB, std::max (std::abs (pl), std::abs (pr)));
            } else {
                float l = lim (bL_[(size_t) s] * g1, 1), rr = clampf (bR_[(size_t) s] * g1 * (limEnv_[1] > 0.95f ? 0.95f / limEnv_[1] : 1.0f), -1, 1);
                if (! std::isfinite (rr)) rr = 0;
                if (numOuts > 0 && outs[0]) outs[0][off + s] = l; if (numOuts > 1 && outs[1]) outs[1][off + s] = rr;
                for (int k = 2; k < numOuts; ++k) if (outs[k]) outs[k][off + s] = 0.0f;
                pkB = std::max (pkB, std::max (std::abs (l), std::abs (rr)));
            }
        }
        outPeakPre.store (pkA); outPeakPost.store (pkB);
        // click: after the limiter, independent of the tuner mute, onto its own outputs (core ClickEngine)
        { const int co = click.clickOut();
          clickConflict.store (click.active() && ((r == Routing::FourCable && co != 2) || (r == Routing::TwoCable && co != 1)));
          click.processRange (outs, numOuts, off, n); }
    }
    // v0.12 load measurement -> oversampling governor (applied at the start of the next block)
    const double blockSec = (double) total / fs_;
    const double load = loadOverride_ >= 0 ? loadOverride_ : std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / std::max (1e-9, blockSec);
    dspLoad.store ((float) (0.9 * dspLoad.load() + 0.1 * load));
    gov_.update (load, blockSec);
    oversamplingNow.store (osApplied_); oversamplingReduced.store (osApplied_ < gov_.user);
}

double RigEngine::latencySamples() const {
    double l = 0; const Chain* c = active_ ? active_ : pending_.load();
    if (c) for (auto& lane : c->lanes) for (auto& s : lane) if (s.pedal->oversamples()) l += Oversampler::latencySamples (oversamplingNow.load());
    return l;
}

} // namespace wr

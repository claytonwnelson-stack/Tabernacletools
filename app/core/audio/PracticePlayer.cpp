#include "PracticePlayer.h"
#include <thread>

namespace wr {

void PracticePlayer::setTrack (std::shared_ptr<const PracticeAudio> t) {
    playing_.store (false);
    while (lock_.exchange (true)) std::this_thread::yield();
    pending_ = std::move (t); swap_.store (true);
    lock_.store (false);
    hasTrack_.store (pending_ && ! pending_->left.empty()); length_.store (pending_ ? pending_->seconds() : 0.0); pos_.store (0.0); seekTo_.store (0.0); loopB_.store (-1.0);
}

void PracticePlayer::renderFrame (const PracticeAudio& t) {
    const long long len = (long long) t.left.size(); const bool stereo = t.right.size() == t.left.size();
    if (win_.size() != (size_t) kFrame) { win_.resize (kFrame); for (int i = 0; i < kFrame; ++i) win_[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * 3.14159265f * (float) i / (float) kFrame); }
    if (olaL_.size() != (size_t) kFrame) { olaL_.assign (kFrame, 0.0f); olaR_.assign (kFrame, 0.0f); }
    auto at = [&] (const std::vector<float>& v, long long i) { return i >= 0 && i < len ? v[(size_t) i] : 0.0f; };
    long long start = (long long) std::llround (anaPos_);
    const double sp = speed_.load();
    if (std::abs (sp - 1.0) < 1e-9 || prevStart_ < 0) {
        // speed 1: frames are consecutive -> overlap-add of Hann at 75 % overlap reconstructs the input exactly
    } else {
        // WSOLA: best start near the target, matching the natural continuation of the previous frame
        const long long nat = prevStart_ + kHop; double best = -1e30; long long bestS = start;
        for (long long d = -kSearch; d <= kSearch; d += 8) {
            const long long s = start + d; if (s < 0) continue; double c = 0;
            for (int i = 0; i < kFrame / 2; i += 4) c += (double) at (t.left, s + i) * at (t.left, nat + i);
            if (c > best) { best = c; bestS = s; }
        }
        start = bestS;
    }
    // speed 1 keeps the exact grid (bit-transparent apart from the constant OLA gain)
    for (int i = 0; i < kFrame; ++i) {
        const float w = win_[(size_t) i] * 0.5f;   // periodic Hann at hop N/4 sums to 2
        olaL_[(size_t) i] += w * at (t.left, start + i);
        olaR_[(size_t) i] += w * (stereo ? at (t.right, start + i) : at (t.left, start + i));
    }
    for (int i = 0; i < kHop; ++i) { fifoL_.push_back (olaL_[(size_t) i]); fifoR_.push_back (olaR_[(size_t) i]); }
    std::copy (olaL_.begin() + kHop, olaL_.end(), olaL_.begin()); std::fill (olaL_.end() - kHop, olaL_.end(), 0.0f);
    std::copy (olaR_.begin() + kHop, olaR_.end(), olaR_.begin()); std::fill (olaR_.end() - kHop, olaR_.end(), 0.0f);
    prevStart_ = start; anaPos_ += kHop * sp;   // the target grid advances at the playback speed; the chosen start only wiggles around it
}

void PracticePlayer::process (float* l, float* r, int n) {
    if (swap_.load() && ! lock_.exchange (true)) { cur_ = std::move (pending_); pending_.reset(); swap_.store (false); lock_.store (false); reset_.store (true); }
    if (! cur_ || cur_->left.empty()) return;
    const PracticeAudio& t = *cur_; const double tsr = t.sampleRate;
    auto restart = [&] (double sec) { anaPos_ = std::max (0.0, sec) * tsr; prevStart_ = -1; std::fill (olaL_.begin(), olaL_.end(), 0.0f); std::fill (olaR_.begin(), olaR_.end(), 0.0f);
        fifoL_.clear(); fifoR_.clear(); fifoRead_ = 0; frac_ = 0;
        // prime: the first kFrame - kHop samples of a fresh OLA are a fade-in; skip them so a seek lands where asked
        anaPos_ -= (kFrame - kHop); for (int k = 0; k < (kFrame - kHop) / kHop; ++k) renderFrame (t); fifoL_.clear(); fifoR_.clear(); };
    if (reset_.exchange (false)) restart (pos_.load());
    const double sk = seekTo_.exchange (-1.0); if (sk >= 0) restart (sk);
    if (! playing_.load()) return;
    const double step = tsr / fs_; const float g = gain_.load();
    const double la = loopA_.load(), lb = loopB_.load();
    for (int i = 0; i < n; ++i) {
        while (fifoRead_ + 2 >= fifoL_.size()) {
            if (fifoRead_ > 4096) { fifoL_.erase (fifoL_.begin(), fifoL_.begin() + (long) fifoRead_); fifoR_.erase (fifoR_.begin(), fifoR_.begin() + (long) fifoRead_); fifoRead_ = 0; }
            renderFrame (t);
        }
        const float a = (float) frac_;
        const float L = fifoL_[fifoRead_] + (fifoL_[fifoRead_ + 1] - fifoL_[fifoRead_]) * a;
        const float R = fifoR_[fifoRead_] + (fifoR_[fifoRead_ + 1] - fifoR_[fifoRead_]) * a;
        if (r) { l[i] += L * g; r[i] += R * g; } else l[i] += 0.5f * (L + R) * g;
        frac_ += step; while (frac_ >= 1.0) { frac_ -= 1.0; ++fifoRead_; }
    }
    // heard position: analysis position minus what is still queued (fifo + overlap), in track seconds of material
    const double queued = (double) (fifoL_.size() - fifoRead_) - frac_;
    const double heard = (anaPos_ - queued * speed_.load()) / tsr;
    pos_.store (std::max (0.0, heard));
    if (lb > 0 && heard >= lb) restart (la);
    else if (heard >= t.seconds()) { playing_.store (false); pos_.store (t.seconds()); }
}

} // namespace wr

#include "Recorder.h"
#include <algorithm>
#include <cmath>

namespace wr {
static void le (FILE* f, uint32_t v, int bytes) { for (int i = 0; i < bytes; ++i) std::fputc ((int) ((v >> (8 * i)) & 0xff), f); }
void RehearsalRecorder::writeHeader (bool) {
    const uint32_t bpsmp = (uint32_t) bits_ / 8, data = (uint32_t) (written_ * ch_ * bpsmp);
    std::fseek (f_, 0, SEEK_SET); std::fwrite ("RIFF", 1, 4, f_); le (f_, 36 + data, 4); std::fwrite ("WAVEfmt ", 1, 8, f_); le (f_, 16, 4); le (f_, 1, 2); le (f_, (uint32_t) ch_, 2);
    le (f_, (uint32_t) sr_, 4); le (f_, (uint32_t) sr_ * ch_ * bpsmp, 4); le (f_, (uint32_t) ch_ * bpsmp, 2); le (f_, (uint32_t) bits_, 2); std::fwrite ("data", 1, 4, f_); le (f_, data, 4);
    std::fseek (f_, 0, SEEK_END);
}
bool RehearsalRecorder::start (const std::string& path, double sr, int ch, int bits) {
    stop(); f_ = std::fopen (path.c_str(), "wb"); if (! f_) return false;
    path_ = path; sr_ = sr; ch_ = std::clamp (ch, 1, 2); bits_ = bits == 16 ? 16 : 24; written_ = 0; dropped_ = 0; r_ = w_.load(); writeHeader (false);
    rec_.store (true); return true;
}
void RehearsalRecorder::push (const float* a, const float* b, int n) {
    if (! rec_.load (std::memory_order_acquire) || ! a) return;
    const size_t cap = ring_.size() / 2; size_t w = w_.load (std::memory_order_relaxed); const size_t r = r_.load (std::memory_order_acquire);
    float pk = peak_.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i) {
        if (w - r >= cap) { dropped_.fetch_add (n - i); break; }
        const size_t k = (w % cap) * 2; ring_[k] = a[i]; ring_[k + 1] = b ? b[i] : 0.0f; ++w; pk = std::max (pk, std::abs (a[i]));
    }
    peak_.store (pk, std::memory_order_relaxed); w_.store (w, std::memory_order_release);
}
size_t RehearsalRecorder::drain() {
    if (! f_) return 0; const size_t cap = ring_.size() / 2; size_t r = r_.load (std::memory_order_relaxed); const size_t w = w_.load (std::memory_order_acquire); size_t n = 0;
    for (; r != w; ++r, ++n) { const size_t k = (r % cap) * 2;
        for (int c = 0; c < ch_; ++c) { const float x = std::clamp (ring_[k + (size_t) c], -1.0f, 1.0f);
            if (bits_ == 16) le (f_, (uint32_t) (int32_t) std::lrint (x * 32767.0f) & 0xffffu, 2); else le (f_, (uint32_t) (int32_t) std::lrint (x * 8388607.0f) & 0xffffffu, 3); } }
    r_.store (r, std::memory_order_release); written_ += (long long) n; return n;
}
void RehearsalRecorder::stop() {
    if (! f_) return; rec_.store (false); drain(); writeHeader (true); std::fclose (f_); f_ = nullptr;
}
} // namespace wr

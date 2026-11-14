#include "MultitrackPlayer.h"
#include <algorithm>
#include <chrono>
#include <cstring>

namespace wr::mt {

// ---------------------------------------------------------------- sources
void MemorySource::read (int64_t s, int n, float* l, float* r) {
    for (int i = 0; i < n; ++i) { const int64_t k = s + i; const bool in = k >= 0 && k < (int64_t) L.size();
        l[i] = in ? L[(size_t) k] : 0.0f; r[i] = in ? (R.empty() ? L[(size_t) k] : R[(size_t) k]) : 0.0f; }
}
static uint32_t u32 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
static uint16_t u16 (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
#if defined(_WIN32)
static int seek64 (std::FILE* f, int64_t o) { return _fseeki64 (f, o, SEEK_SET); }
#else
static int seek64 (std::FILE* f, int64_t o) { return fseeko (f, (off_t) o, SEEK_SET); }
#endif

std::unique_ptr<WavFileSource> WavFileSource::open (const std::string& path, std::string* err) {
    auto fail = [&] (const char* m) { if (err) *err = m; return std::unique_ptr<WavFileSource>(); };
    std::FILE* f = std::fopen (path.c_str(), "rb"); if (! f) return fail ("Can't open the file");
    std::unique_ptr<WavFileSource> w (new WavFileSource()); w->f_ = f;
    uint8_t h[12]; if (std::fread (h, 1, 12, f) != 12 || std::memcmp (h, "RIFF", 4) || std::memcmp (h + 8, "WAVE", 4)) return fail ("Not a WAV file");
    int fmtTag = 0, blockAlign = 0; bool haveFmt = false, haveData = false; int64_t dataLen = 0;
    std::vector<std::pair<uint32_t, uint32_t>> cues; std::vector<std::pair<uint32_t, std::string>> labels;
    for (;;) {
        uint8_t ch[8]; if (std::fread (ch, 1, 8, f) != 8) break;
        const uint32_t len = u32 (ch + 4); const long pos = std::ftell (f);
        if (! std::memcmp (ch, "fmt ", 4) && len >= 16) {
            std::vector<uint8_t> b (len); if (std::fread (b.data(), 1, len, f) != len) return fail ("Broken WAV header");
            fmtTag = u16 (&b[0]); w->ch_ = u16 (&b[2]); w->rate_ = u32 (&b[4]); blockAlign = u16 (&b[12]); w->bits_ = u16 (&b[14]);
            if (fmtTag == 0xFFFE && len >= 26) fmtTag = u16 (&b[24]);
            haveFmt = true;
        } else if (! std::memcmp (ch, "data", 4)) { w->dataOff_ = pos; dataLen = len; haveData = true; }
        else if (! std::memcmp (ch, "cue ", 4) && len >= 4) {
            std::vector<uint8_t> b (len); if (std::fread (b.data(), 1, len, f) == len) { const uint32_t n = u32 (&b[0]);
                for (uint32_t i = 0; i < n && 4 + (i + 1) * 24 <= len; ++i) cues.push_back ({ u32 (&b[4 + i * 24]), u32 (&b[4 + i * 24 + 20]) }); }
        } else if (! std::memcmp (ch, "LIST", 4) && len >= 4) {
            std::vector<uint8_t> b (len); if (std::fread (b.data(), 1, len, f) == len && ! std::memcmp (&b[0], "adtl", 4))
                for (size_t o = 4; o + 8 <= len;) { const uint32_t sl = u32 (&b[o + 4]);
                    if (! std::memcmp (&b[o], "labl", 4) && sl >= 4 && o + 8 + sl <= len) { std::string t ((const char*) &b[o + 12], sl - 4); t = t.c_str(); labels.push_back ({ u32 (&b[o + 8]), t }); }
                    o += 8 + sl + (sl & 1); }
        }
        if (seek64 (f, (int64_t) pos + len + (len & 1))) break;
    }
    if (! haveFmt || ! haveData) return fail ("WAV file has no audio");
    w->float_ = fmtTag == 3;
    if (! ((fmtTag == 1 && (w->bits_ == 16 || w->bits_ == 24 || w->bits_ == 32)) || (fmtTag == 3 && w->bits_ == 32))) return fail ("Unsupported WAV encoding");
    if (w->ch_ < 1 || w->ch_ > 2 || w->rate_ < 1000 || blockAlign != w->ch_ * w->bits_ / 8) return fail ("Unsupported WAV layout (mono or stereo only)");
    w->frames_ = dataLen / blockAlign;
    for (auto& c : cues) { FileMarker m; m.sec = c.second / w->rate_; for (auto& l : labels) if (l.first == c.first) m.label = l.second; w->markers_.push_back (m); }
    std::sort (w->markers_.begin(), w->markers_.end(), [] (auto& a, auto& b) { return a.sec < b.sec; });
    return w;
}
void WavFileSource::read (int64_t start, int n, float* l, float* r) {
    std::fill (l, l + n, 0.0f); std::fill (r, r + n, 0.0f);
    int64_t a = std::max<int64_t> (start, 0), b = std::min<int64_t> (start + n, frames_); if (b <= a || ! f_) return;
    const int bpf = ch_ * bits_ / 8; const size_t bytes = (size_t) (b - a) * (size_t) bpf; raw_.resize (bytes);
    if (seek64 (f_, dataOff_ + a * bpf)) return;
    const size_t got = std::fread (raw_.data(), 1, bytes, f_); const int64_t fr = (int64_t) got / bpf;
    for (int64_t i = 0; i < fr; ++i) {
        float s[2] = { 0, 0 };
        for (int c = 0; c < ch_; ++c) { const uint8_t* p = &raw_[(size_t) (i * bpf + c * bits_ / 8)];
            if (float_) { float v; std::memcpy (&v, p, 4); s[c] = v; }
            else if (bits_ == 16) s[c] = (int16_t) u16 (p) / 32768.0f;
            else if (bits_ == 24) { int32_t v = (int32_t) ((uint32_t) p[0] << 8 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 24); s[c] = (float) (v >> 8) / 8388608.0f; }
            else s[c] = (float) ((double) (int32_t) u32 (p) / 2147483648.0); }
        const size_t k = (size_t) (a - start + i); l[k] = s[0]; r[k] = ch_ > 1 ? s[1] : s[0];
    }
}
bool writeWavFloat (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, double rate, const std::vector<FileMarker>& marks) {
    std::FILE* f = std::fopen (path.c_str(), "wb"); if (! f) return false;
    auto w32 = [&] (uint32_t v) { uint8_t b[4] = { (uint8_t) v, (uint8_t) (v >> 8), (uint8_t) (v >> 16), (uint8_t) (v >> 24) }; std::fwrite (b, 1, 4, f); };
    auto w16 = [&] (uint16_t v) { uint8_t b[2] = { (uint8_t) v, (uint8_t) (v >> 8) }; std::fwrite (b, 1, 2, f); };
    const uint32_t n = (uint32_t) l.size(), data = n * 8;
    uint32_t cueLen = marks.empty() ? 0 : 4 + 24 * (uint32_t) marks.size(), listLen = 0;
    for (auto& m : marks) { uint32_t sl = 4 + (uint32_t) m.label.size() + 1; listLen += 8 + sl + (sl & 1); } if (! marks.empty()) listLen += 4;
    std::fwrite ("RIFF", 1, 4, f); w32 (4 + 24 + 8 + data + (cueLen ? 8 + cueLen : 0) + (listLen ? 8 + listLen : 0)); std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f); w32 (16); w16 (3); w16 (2); w32 ((uint32_t) rate); w32 ((uint32_t) rate * 8); w16 (8); w16 (32);
    std::fwrite ("data", 1, 4, f); w32 (data);
    for (uint32_t i = 0; i < n; ++i) { float a = l[i], b = i < r.size() ? r[i] : l[i]; std::fwrite (&a, 4, 1, f); std::fwrite (&b, 4, 1, f); }
    if (cueLen) { std::fwrite ("cue ", 1, 4, f); w32 (cueLen); w32 ((uint32_t) marks.size());
        for (size_t i = 0; i < marks.size(); ++i) { const uint32_t s = (uint32_t) std::llround (marks[i].sec * rate); w32 ((uint32_t) i + 1); w32 (s); std::fwrite ("data", 1, 4, f); w32 (0); w32 (0); w32 (s); }
        std::fwrite ("LIST", 1, 4, f); w32 (listLen); std::fwrite ("adtl", 1, 4, f);
        for (size_t i = 0; i < marks.size(); ++i) { uint32_t sl = 4 + (uint32_t) marks[i].label.size() + 1; std::fwrite ("labl", 1, 4, f); w32 (sl); w32 ((uint32_t) i + 1);
            std::fwrite (marks[i].label.c_str(), 1, marks[i].label.size() + 1, f); if (sl & 1) std::fputc (0, f); } }
    return std::fclose (f) == 0;
}

bool WavFloatWriter::open (const std::string& path, double rate) {
    if (f_) close(); f_ = std::fopen (path.c_str(), "wb"); if (! f_) return false; rate_ = rate; frames_ = 0;
    uint8_t h[44] = {}; std::fwrite (h, 1, 44, f_); return true;   // header written at close
}
bool WavFloatWriter::write (const float* l, const float* r, int n) {
    if (! f_ || n <= 0) return f_ != nullptr; buf_.resize ((size_t) n * 2);
    for (int i = 0; i < n; ++i) { buf_[(size_t) i * 2] = l[i]; buf_[(size_t) i * 2 + 1] = r ? r[i] : l[i]; }
    if (std::fwrite (buf_.data(), sizeof (float), buf_.size(), f_) != buf_.size()) return false;
    frames_ += n; return true;
}
bool WavFloatWriter::close (const std::vector<FileMarker>& marks) {
    if (! f_) return false; std::FILE* f = f_; f_ = nullptr;
    auto w32 = [&] (uint32_t v) { uint8_t b[4] = { (uint8_t) v, (uint8_t) (v >> 8), (uint8_t) (v >> 16), (uint8_t) (v >> 24) }; std::fwrite (b, 1, 4, f); };
    auto w16 = [&] (uint16_t v) { uint8_t b[2] = { (uint8_t) v, (uint8_t) (v >> 8) }; std::fwrite (b, 1, 2, f); };
    const uint32_t data = (uint32_t) (frames_ * 8);
    uint32_t cueLen = marks.empty() ? 0 : 4 + 24 * (uint32_t) marks.size(), listLen = 0;
    for (auto& m : marks) { uint32_t sl = 4 + (uint32_t) m.label.size() + 1; listLen += 8 + sl + (sl & 1); } if (! marks.empty()) listLen += 4;
    if (cueLen) { std::fwrite ("cue ", 1, 4, f); w32 (cueLen); w32 ((uint32_t) marks.size());
        for (size_t i = 0; i < marks.size(); ++i) { const uint32_t s = (uint32_t) std::llround (marks[i].sec * rate_); w32 ((uint32_t) i + 1); w32 (s); std::fwrite ("data", 1, 4, f); w32 (0); w32 (0); w32 (s); }
        std::fwrite ("LIST", 1, 4, f); w32 (listLen); std::fwrite ("adtl", 1, 4, f);
        for (size_t i = 0; i < marks.size(); ++i) { uint32_t sl = 4 + (uint32_t) marks[i].label.size() + 1; std::fwrite ("labl", 1, 4, f); w32 (sl); w32 ((uint32_t) i + 1);
            std::fwrite (marks[i].label.c_str(), 1, marks[i].label.size() + 1, f); if (sl & 1) std::fputc (0, f); } }
    std::fseek (f, 0, SEEK_SET);
    std::fwrite ("RIFF", 1, 4, f); w32 (4 + 24 + 8 + data + (cueLen ? 8 + cueLen : 0) + (listLen ? 8 + listLen : 0)); std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f); w32 (16); w16 (3); w16 (2); w32 ((uint32_t) rate_); w32 ((uint32_t) rate_ * 8); w16 (8); w16 (32);
    std::fwrite ("data", 1, 4, f); w32 (data);
    return std::fclose (f) == 0;
}

// ---------------------------------------------------------------- player
bool MultitrackPlayer::load (const MultitrackSet& set, std::vector<std::unique_ptr<StemSource>> src, std::vector<Segment> seq, bool sync) {
    unload(); sync_ = sync; mismatch_ = false;
    const int n = std::min ((int) src.size(), kMaxStems);
    for (int i = 0; i < n; ++i) { if (! src[(size_t) i]) continue; auto d = std::make_unique<StemData>();
        if (std::abs (src[(size_t) i]->sampleRate() - fs_) > 0.5) mismatch_ = true;
        d->src = std::move (src[(size_t) i]); for (auto& s : d->slots) { s.l.assign (kChunk, 0.0f); s.r.assign (kChunk, 0.0f); }
        st_.push_back (std::move (d)); }
    nStems_ = (int) st_.size(); if (! nStems_) return false;
    tl_.assign (kMaxBlock, 0.0f); tr_.assign (kMaxBlock, 0.0f);
    tempo_ = set.tempo; bpb_ = set.beatsPerBar; rate_ = set.sampleRate;
    seq_ = Sequencer(); seq_.setSequence (std::move (seq), tempo_, bpb_, rate_);
    setMix (set);
    for (int i = 0; i < nStems_; ++i) { st_[(size_t) i]->curGain = st_[(size_t) i]->gain.load(); st_[(size_t) i]->curPan = st_[(size_t) i]->pan.load(); }
    pos_.store (seq_.srcPos()); target_.store (seq_.nextTargetSrc()); cur_.store (seq_.current());
    C c; while (cmds_.pop (c)) {}
    fillOnce();                                         // first chunks ready before PLAY
    quit_.store (false); loaded_.store (true);
    if (! sync_) reader_ = std::thread ([this] { readerLoop(); });
    return true;
}
void MultitrackPlayer::unload() {
    while (busy_.exchange (true)) std::this_thread::yield();
    loaded_.store (false); busy_.store (false);
    quit_.store (true); cv_.notify_all(); if (reader_.joinable()) reader_.join();
    st_.clear(); nStems_ = 0; playing_.store (false); cur_.store (-1); loopSec_.store (-1); pending_.store (-1);
}
void MultitrackPlayer::setSequence (std::vector<Segment> seq) {
    // v0.16: hand the new sequence to the audio thread (swapped in at the top of the next block, no lock, no dropped block).
    // Wait (briefly) for a previous hand-off; if audio isn't running, apply it directly under the lock.
    for (int i = 0; i < 100 && seqPending_.load (std::memory_order_acquire); ++i) std::this_thread::sleep_for (std::chrono::milliseconds (1));
    if (seqPending_.load()) { while (busy_.exchange (true)) std::this_thread::yield(); seqPending_.store (false); seq_.resequence (std::move (pendingSeq_)); busy_.store (false); }
    pendingSeq_ = std::move (seq); seqPending_.store (true, std::memory_order_release);
    if (sync_) return;   // tests: applied by the next process()
}
void MultitrackPlayer::setMix (const MultitrackSet& set) {
    for (int i = 0; i < nStems_ && i < (int) set.stems.size(); ++i) { auto& d = *st_[(size_t) i]; const auto& s = set.stems[(size_t) i];
        d.gain.store (stemGain (set, (size_t) i)); d.pan.store (s.pan); d.bus.store ((int) s.bus); }
}
bool MultitrackPlayer::readCached (StemData& s, int64_t src, int len, float* l, float* r) {
    bool ok = true; int done = 0;
    while (done < len) {
        const int64_t p = src + done; if (p < 0) { l[done] = r[done] = 0; ++done; continue; }
        const int64_t c = p / kChunk; const int off = (int) (p - c * kChunk), k = std::min (len - done, kChunk - off);
        Slot* hit = nullptr; for (auto& sl : s.slots) if (sl.chunk.load (std::memory_order_acquire) == c) { hit = &sl; break; }
        if (hit) { std::memcpy (l + done, hit->l.data() + off, sizeof (float) * (size_t) k); std::memcpy (r + done, hit->r.data() + off, sizeof (float) * (size_t) k);
                   if (hit->chunk.load (std::memory_order_acquire) != c) hit = nullptr; }
        if (! hit) { std::fill (l + done, l + done + k, 0.0f); std::fill (r + done, r + done + k, 0.0f); ok = false; }
        done += k;
    }
    return ok;
}
void MultitrackPlayer::ensure (StemData& s, int64_t c, const int64_t* want, int nWant) {
    for (auto& sl : s.slots) if (sl.chunk.load() == c) return;
    Slot* v = nullptr; int64_t far = -1; const int64_t here = want[0];
    for (auto& sl : s.slots) { const int64_t id = sl.chunk.load(); if (id < 0) { v = &sl; break; }
        bool wanted = false; for (int i = 0; i < nWant; ++i) wanted |= want[i] == id; if (wanted) continue;
        const int64_t d = id > here ? id - here : here - id; if (d > far) { far = d; v = &sl; } }
    if (! v) return;
    v->chunk.store (-1, std::memory_order_release);
    s.src->read (c * kChunk, kChunk, v->l.data(), v->r.data());
    v->chunk.store (c, std::memory_order_release);
}
void MultitrackPlayer::fillOnce() {
    int64_t want[kAhead + kTargetAhead]; int nw = 0;
    const int64_t c0 = std::max<int64_t> (0, pos_.load()) / kChunk; for (int i = 0; i < kAhead; ++i) want[nw++] = c0 + i;
    const int64_t t = target_.load(); if (t >= 0) for (int i = 0; i < kTargetAhead; ++i) want[nw++] = t / kChunk + i;
    for (auto& s : st_) for (int i = 0; i < nw; ++i) ensure (*s, want[i], want, nw);
}
void MultitrackPlayer::readerLoop() {
    while (! quit_.load()) {
        fillOnce();
        std::unique_lock<std::mutex> lk (m_); cv_.wait_for (lk, std::chrono::milliseconds (5), [this] { return quit_.load(); });
    }
}
void MultitrackPlayer::process (float* l, float* r, float* cue, int n) {
    for (int o = 0; o < n; o += kMaxBlock) { const int k = std::min (kMaxBlock, n - o); processBlock (l ? l + o : nullptr, r ? r + o : nullptr, cue ? cue + o : nullptr, k); }
}
void MultitrackPlayer::processBlock (float* L, float* R, float* cue, int n) {
    if (! loaded_.load() || busy_.exchange (true)) return;
    if (seqPending_.load (std::memory_order_acquire)) { seq_.resequence (std::move (pendingSeq_)); seqPending_.store (false, std::memory_order_release); } // swap: no allocation
    C c; while (cmds_.pop (c)) switch (c.t) {
        case C::Play: seq_.play ((int64_t) std::llround (std::max (0.0f, c.v) * fs_)); break;
        case C::Stop: seq_.stop(); break;
        case C::Locate: seq_.locate (c.i); break;
        case C::Jump: seq_.requestJump (c.i, (Quant) (int) c.v); break;
        case C::CancelJump: seq_.cancelJump(); break;
        case C::Loop: seq_.setLoop (c.i != 0); break;
        default: break; }
    pos_.store (seq_.srcPos()); target_.store (seq_.nextTargetSrc());
    if (sync_) fillOnce();
    const int ns = seq_.render (n, spans_.data(), kMaxSpans);
    const float master = master_.load();
    for (auto& sp : st_) { auto& s = *sp;
        const float gA = s.curGain, gB = s.gain.load() * master; s.curGain = gB;
        s.curPan += (s.pan.load() - s.curPan) * 0.5f; float pl, pr; panGains (s.curPan, pl, pr);
        if (gA == 0 && gB == 0) continue;
        const bool toCue = s.bus.load() == (int) Bus::Cue;
        for (int k = 0; k < ns; ++k) { const auto& x = spans_[(size_t) k]; if (x.len <= 0) continue;
            if (! readCached (s, x.src, x.len, tl_.data(), tr_.data())) underruns_.fetch_add (1);
            for (int j = 0; j < x.len; ++j) { const int p = x.dst + j;
                const float g = (gA + (gB - gA) * (float) p / (float) n) * (x.g0 + (x.g1 - x.g0) * (float) j / (float) x.len);
                if (toCue) { if (cue) cue[p] += 0.5f * (tl_[(size_t) j] + tr_[(size_t) j]) * g; }
                else { if (L) L[p] += tl_[(size_t) j] * g * pl; if (R) R[p] += tr_[(size_t) j] * g * pr; } } } }
    playing_.store (seq_.playing()); ended_.store (seq_.ended()); cur_.store (seq_.current()); loopSec_.store (seq_.loopSection()); pending_.store (seq_.pendingJump());
    beat_.store (seq_.beat()); bar_.store (seq_.barInSection()); changes_.store (seq_.sectionChanges());
    pos_.store (seq_.srcPos()); target_.store (seq_.nextTargetSrc());
    busy_.store (false);
}

} // namespace wr::mt

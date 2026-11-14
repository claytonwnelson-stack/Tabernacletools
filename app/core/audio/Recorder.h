// v0.8 rehearsal recorder (core, any app): the audio thread pushes blocks into a lock-free ring; the message thread
// drains it into a WAV file (16-bit or 24-bit PCM, 1 or 2 channels: ch1 = instrument input, ch2 = click when enabled).
// Realtime-safe push (no allocation, no locks, drops + counts samples if the writer falls behind).
#pragma once
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace wr {
class RehearsalRecorder {
public:
    explicit RehearsalRecorder (size_t ringFrames = 1 << 18) : ring_ (ringFrames * 2) {}
    ~RehearsalRecorder() { stop(); }
    bool start (const std::string& path, double sampleRate, int channels, int bits = 24); // message thread
    void stop();                                                                     // drains + finalises the header
    bool recording() const { return rec_.load(); }
    // audio thread: ch2 may be null (mono file, or silence in ch2 of a stereo file)
    void push (const float* ch1, const float* ch2, int n);
    size_t drain();                                       // message thread (timer): writes what is buffered; returns frames written
    double seconds() const { return sr_ > 0 ? (double) written_ / sr_ : 0; }
    long long framesWritten() const { return written_; }
    long long dropped() const { return dropped_.load(); }
    const std::string& path() const { return path_; }
    float peak() const { return peak_.load(); }           // since the last call to clearPeak (input meter)
    void clearPeak() { peak_.store (0); }
private:
    void writeHeader (bool final);
    std::vector<float> ring_; std::atomic<size_t> w_ { 0 }, r_ { 0 }; std::atomic<bool> rec_ { false };
    std::atomic<long long> dropped_ { 0 }; std::atomic<float> peak_ { 0 };
    FILE* f_ = nullptr; std::string path_; double sr_ = 0; int ch_ = 1, bits_ = 24; long long written_ = 0;
};
} // namespace wr

// Spoken section cues + count-in voice: section-name -> words, WAV decoding, a word bank resampled to the engine rate,
// and phrase building. Message thread only (the audio thread just plays finished VoiceClips).
#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace wr {
struct VoiceClip { const float* data = nullptr; int len = 0; };

// "Verse 1" -> {"verse","one"}; "Pre-Chorus" -> {"pre-chorus"}; "Final Chorus x2" -> {"final","chorus"}; "Bridge 10" -> {"bridge","ten"}
std::vector<std::string> cueWords (const std::string& sectionName);
std::string spokenName (const std::string& sectionName);       // "Verse one" (for TTS / display)
std::string numberWord (int n);                                 // 1..12 -> "one".."twelve", else ""

bool decodeWav (const uint8_t* data, size_t size, std::vector<float>& mono, double& sampleRate); // PCM 16/24/32-bit int or 32-bit float

class VoiceBank {
public:
    void addWord (const std::string& word, std::vector<float> mono, double sampleRate); // stored at the source rate
    void addPhrase (const std::string& key, std::vector<float> mono, double sampleRate); // TTS render for a custom name (whole phrase)
    bool hasWord (const std::string& w) const { return words_.count (w) > 0; }
    bool hasPhrase (const std::string& k) const { return phrases_.count (k) > 0; }
    bool covers (const std::string& sectionName) const;          // every word known (or a phrase render exists)
    void prepare (double engineRate);                            // resample everything for the engine
    // phrase for a section name at the engine rate: bundled words with short gaps; a TTS phrase if words are missing;
    // otherwise just the known words. Empty if nothing could be spoken.
    std::vector<float> phrase (const std::string& sectionName) const;
    VoiceClip number (int n) const;                              // count words at the engine rate (1..12)
    double rate() const { return fs_; }
    int size() const { return (int) words_.size(); }
private:
    struct Src { std::vector<float> raw; double sr = 0; std::vector<float> out; };
    static void resample (Src& s, double fs);
    std::map<std::string, Src> words_, phrases_; double fs_ = 48000;
};
} // namespace wr

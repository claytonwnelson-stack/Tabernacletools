#include "AppStore.h"
#include "Platform.h"

namespace wr {
juce::File AppStore::file() const { return platform::dataDirectory().getChildFile ("library.json"); }

void AppStore::load() {
    auto dir = platform::dataDirectory(); dir.createDirectory();
    backups_ = std::make_unique<BackupStore> (dir.getFullPathName().toStdString(), 10, 60);
    auto r = backups_->open();
    if (r.source != BackupStore::Source::None) {
        bool ok = false; Json j = Json::parse (r.json, &ok);
        if (ok) {
            lib = libraryFromJson (j); if (lib.controllers.empty()) lib.controllers = { defaultGuitarUnit(), defaultSectionUnit() }; if (lib.rigs.empty()) lib.rigs.push_back (defaultRig());
            if (r.source == BackupStore::Source::Backup) recoveryNote = "The library file was damaged - restored the newest backup (" + juce::File (juce::String (r.restoredFrom)).getFileName() + ").";
            else if (r.crashed) recoveryNote = "Tabernacle Tools didn't close cleanly last time. Your last autosave was loaded (backups: Setlist > Backups).";
            return;
        }
    }
    lib = seedLibrary(); save();
}

void AppStore::save() {
    if (! backups_) { backups_ = std::make_unique<BackupStore> (platform::dataDirectory().getFullPathName().toStdString(), 10, 60); }
    backups_->save (toJson (lib).dump (1));
    dirty = false; lastSavedTime = juce::Time::getCurrentTime().toString (false, true, false);
}

bool AppStore::restoreBackup (const BackupInfo& b) {
    if (! backups_) return false; if (dirty) save();
    std::string json; if (! backups_->restore (b, json)) return false;
    bool ok = false; Json j = Json::parse (json, &ok); if (! ok) return false;
    lib = libraryFromJson (j); if (lib.controllers.empty()) lib.controllers = { defaultGuitarUnit(), defaultSectionUnit() }; if (lib.rigs.empty()) lib.rigs.push_back (defaultRig());
    dirty = false; return true;
}

juce::Array<juce::File> AppStore::exportSetlist (const Setlist& set, const std::vector<const Player*>& players, ExportFormat f) {
    juce::Array<juce::File> out; auto dir = platform::dataDirectory().getChildFile ("Exports"); dir.createDirectory();
    for (auto& ef : wr::exportSetlist (lib, set, players, f)) { auto file = dir.getChildFile (juce::String::fromUTF8 (ef.name.c_str())); file.replaceWithData (ef.data.data(), ef.data.size()); out.add (file); }
    return out;
}

static std::string slug (const juce::String& t) {
    juce::String s; for (auto c : t.toLowerCase()) s += juce::CharacterFunctions::isLetterOrDigit (c) ? juce::String::charToString (c) : juce::String ("-");
    while (s.contains ("--")) s = s.replace ("--", "-");
    return s.trimCharactersAtStart ("-").trimCharactersAtEnd ("-").toStdString();
}

juce::String AppStore::importChartText (const juce::String& text, const juce::String& title) {
    return addSong (parseChart (text.toStdString(), title.toStdString()));
}

juce::String AppStore::addSong (Chart chart) {
    Song s; s.chart = std::move (chart);
    std::string base = slug (juce::String (s.chart.title)); if (base.empty()) base = "song"; std::string id = base; int n = 2;
    while (lib.song (id)) id = base + "-" + std::to_string (n++);
    s.id = id; s.rigPreset = lib.rigs.empty() ? "Worship Ambient" : lib.rigs.front().name;
    if (auto* seed = lib.song ("he-who-was")) { s.scenes = seed->scenes; s.sectionScene = seed->sectionScene; s.defaultScene = seed->defaultScene; } // sensible starter scenes
    lib.songs.push_back (s); save();
    return id;
}

juce::String AppStore::importChart (const juce::File& f, juce::String& error) {
    juce::String text;
    if (f.hasFileExtension ("pdf")) {
        text = platform::extractPdfText (f);
        if (text.trim().isEmpty()) { error = "This PDF has no text layer (it's a scan or photo). Photo/scan OCR is planned for v0.2 - for now export the chart as text or ChordPro."; return {}; }
    } else if (f.hasFileExtension ("png;jpg;jpeg;heic")) {
        error = "Photo import (OCR) is planned for v0.2. Use ChordPro, text, or a text-based PDF."; return {};
    } else text = f.loadFileAsString();
    auto id = importChartText (text, f.getFileNameWithoutExtension());
    if (auto* s = lib.song (id.toStdString())) if (s->chart.sections.empty()) error = "No sections or chords were recognised - check the file.";
    return id;
}
}

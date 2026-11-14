// Persistence of the Library (songs, setlists, rigs, controller profiles, settings) as JSON in the app data dir.
#pragma once
#include <JuceHeader.h>
#include "model/Song.h"
#include "model/Chart.h"
#include "model/Backup.h"
#include "model/Export.h"

namespace wr {
class AppStore {
public:
    Library lib;
    juce::File file() const;
    void load();                 // seeds the library on first run
    void save();
    void saveSoon() { dirty = true; }
    bool dirty = false;
    // Import a ChordPro / text / text-PDF chart. Returns the new song id or empty + error.
    juce::String importChart (const juce::File& f, juce::String& error);
    juce::String importChartText (const juce::String& text, const juce::String& title);
    juce::String addSong (wr::Chart chart);       // v0.7: add a parsed chart (import preview) as a new song; returns its id
    juce::String lastSavedTime;
    // autosave + rotating backups (last 10) + crash recovery
    juce::String recoveryNote;                       // shown once after a crash / restore
    std::vector<BackupInfo> backups() const { return backups_ ? backups_->list() : std::vector<BackupInfo>(); }
    bool restoreBackup (const BackupInfo& b);       // current state is snapshotted first
    void closeClean() { if (dirty) save(); if (backups_) backups_->closeClean(); }
    // setlist sharing: writes files to <data>/Exports and returns them (the UI hands them to the share sheet)
    juce::Array<juce::File> exportSetlist (const Setlist& set, const std::vector<const Player*>& players, ExportFormat f);
private:
    std::unique_ptr<BackupStore> backups_;
};
}

// Autosave + rotating backups + crash recovery for the library (songs, setlists, rigs, settings). Pure C++17.
// Layout in dir:  library.json (current, written atomically via tmp + rename), running.lock (present while the app runs),
//                 backups/backup-<seq>-<unixtime>.json (newest kept; snapshots < coalesceSec apart replace the newest).
#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace wr {
struct BackupInfo { std::string path; long long seq = 0; long long unixTime = 0; size_t bytes = 0; };

class BackupStore {
public:
    explicit BackupStore (std::string dir, int keep = 10, int coalesceSec = 60) : dir_ (std::move (dir)), keep_ (keep), coalesce_ (coalesceSec) {}
    enum class Source { Main, Backup, None };
    struct LoadResult { std::string json; Source source = Source::None; std::string restoredFrom; bool crashed = false; bool mainWasBad = false; };

    // startup: detects an unclean previous exit (lock left behind), loads library.json, or the newest *valid* backup if it's missing/corrupt
    LoadResult open();
    void closeClean();                                  // remove the lock (normal quit / app backgrounded)
    // every change: write library.json atomically, then snapshot to backups/ if the content changed
    bool save (const std::string& json, long long nowUnix = -1);
    std::vector<BackupInfo> list() const;               // newest first
    bool read (const BackupInfo& b, std::string& out) const;
    // restore: snapshots the current file first (so a restore can be undone), then makes the backup current
    bool restore (const BackupInfo& b, std::string& jsonOut, long long nowUnix = -1);
    std::string mainPath() const { return dir_ + "/library.json"; }
    std::string backupDir() const { return dir_ + "/backups"; }
    static bool validLibraryJson (const std::string& s);   // parses and looks like a library
private:
    bool snapshot (const std::string& json, long long nowUnix, bool forceNew);
    std::string dir_; int keep_, coalesce_; std::string lastSnap_;
};
} // namespace wr

#include "Backup.h"
#include "Json.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <ctime>

namespace fs = std::filesystem;
namespace wr {
static bool readAll (const std::string& p, std::string& out) { std::ifstream f (p, std::ios::binary); if (! f) return false; std::stringstream ss; ss << f.rdbuf(); out = ss.str(); return true; }
static bool writeAtomic (const std::string& p, const std::string& data) {
    std::string tmp = p + ".tmp";
    { std::ofstream f (tmp, std::ios::binary | std::ios::trunc); if (! f) return false; f << data; f.flush(); if (! f) return false; }
    std::error_code ec; fs::rename (tmp, p, ec); if (ec) { fs::remove (p, ec); fs::rename (tmp, p, ec); } return ! ec;
}
static long long nowOr (long long t) { return t >= 0 ? t : (long long) std::time (nullptr); }

bool BackupStore::validLibraryJson (const std::string& s) { bool ok = false; Json j = Json::parse (s, &ok); return ok && j.isObject() && j.has ("songs") && j["songs"].isArray(); }

std::vector<BackupInfo> BackupStore::list() const {
    std::vector<BackupInfo> v; std::error_code ec;
    if (! fs::is_directory (backupDir(), ec)) return v;
    for (auto& e : fs::directory_iterator (backupDir(), ec)) {
        auto n = e.path().filename().string(); long long seq = 0, t = 0;
        if (n.rfind ("backup-", 0) != 0 || std::sscanf (n.c_str(), "backup-%lld-%lld.json", &seq, &t) != 2) continue;
        v.push_back ({ e.path().string(), seq, t, (size_t) e.file_size (ec) });
    }
    std::sort (v.begin(), v.end(), [] (const BackupInfo& a, const BackupInfo& b) { return a.seq > b.seq; });
    return v;
}
bool BackupStore::read (const BackupInfo& b, std::string& out) const { return readAll (b.path, out); }

BackupStore::LoadResult BackupStore::open() {
    LoadResult r; std::error_code ec; fs::create_directories (backupDir(), ec);
    const std::string lock = dir_ + "/running.lock";
    r.crashed = fs::exists (lock, ec);
    { std::ofstream f (lock); f << nowOr (-1); }
    std::string s;
    if (readAll (mainPath(), s) && validLibraryJson (s)) { r.json = s; r.source = Source::Main; lastSnap_ = s; return r; }
    r.mainWasBad = fs::exists (mainPath(), ec);
    if (r.mainWasBad) fs::copy_file (mainPath(), dir_ + "/library.corrupt.json", fs::copy_options::overwrite_existing, ec);
    for (auto& b : list()) if (readAll (b.path, s) && validLibraryJson (s)) { r.json = s; r.source = Source::Backup; r.restoredFrom = b.path; writeAtomic (mainPath(), s); lastSnap_ = s; return r; }
    return r;
}
void BackupStore::closeClean() { std::error_code ec; fs::remove (dir_ + "/running.lock", ec); }

bool BackupStore::snapshot (const std::string& json, long long now, bool forceNew) {
    if (json == lastSnap_ && ! forceNew) return true;
    std::error_code ec; fs::create_directories (backupDir(), ec);
    auto v = list(); long long seq = v.empty() ? 1 : v.front().seq + 1;
    if (! forceNew && ! v.empty() && now - v.front().unixTime < coalesce_ && now >= v.front().unixTime) { seq = v.front().seq; fs::remove (v.front().path, ec); v.erase (v.begin()); }
    std::string p = backupDir() + "/backup-" + std::to_string (seq) + "-" + std::to_string (now) + ".json";
    if (! writeAtomic (p, json)) return false; lastSnap_ = json;
    v = list(); for (size_t i = (size_t) std::max (1, keep_); i < v.size(); ++i) fs::remove (v[i].path, ec);
    return true;
}
bool BackupStore::save (const std::string& json, long long now) {
    std::error_code ec; fs::create_directories (dir_, ec);
    if (! writeAtomic (mainPath(), json)) return false;
    return snapshot (json, nowOr (now), false);
}
bool BackupStore::restore (const BackupInfo& b, std::string& out, long long now) {
    std::string s; if (! readAll (b.path, s) || ! validLibraryJson (s)) return false;
    std::string cur; if (readAll (mainPath(), cur) && validLibraryJson (cur) && cur != s) { lastSnap_.clear(); snapshot (cur, nowOr (now), true); }
    if (! writeAtomic (mainPath(), s)) return false; lastSnap_ = s; out = s; return true;
}
} // namespace wr

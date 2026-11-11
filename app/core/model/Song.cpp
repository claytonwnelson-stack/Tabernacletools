#include "Song.h"
#include <algorithm>
#include "Chart.h"

namespace wr {
const char* seedChartText(); // SeedChart.cpp (generated)

const Scene* Song::sceneForSection (const Section& s) const {
    auto it = sectionScene.find (s.name); if (it == sectionScene.end()) it = sectionScene.find (s.label);
    if (it == sectionScene.end()) it = sectionScene.find (s.kind);
    if (it != sectionScene.end()) return scene (it->second);
    return defaultScene.empty() ? nullptr : scene (defaultScene);
}

bool MidiBinding::matches (int st, int d1, int profileCh, bool ignoreChannel) const {
    if (kind == None) return false;
    int ch = (st & 0x0F) + 1, type = st & 0xF0, want = channel > 0 ? channel : profileCh;
    if (! ignoreChannel && ch != want) return false;
    if (kind == CC && type == 0xB0) {
        if (d1 == number) return true;
        // Chocolate "Manufacturer Control" (CubeSuite) sends CC 20-23. The built-in
        // map is CC 80-83. Treat those as the same four switches.
        return number >= 80 && number <= 83 && d1 == number - 60;
    }
    if (kind == PC) return type == 0xC0 && d1 == number;
    return (type == 0x90 || type == 0x80) && d1 == number;
}

// ------------------------------------------------------------------ JSON helpers
static Json params (const std::map<std::string, float>& m) { Json o = Json::object(); for (auto& kv : m) o.set (kv.first, Json (kv.second)); return o; }
static std::map<std::string, float> paramsFrom (const Json& j) { std::map<std::string, float> m; for (auto& kv : j.members()) m[kv.first] = (float) kv.second.num(); return m; }

Json toJson (const Chart& c) {
    Json j = Json::object(); j.set ("title", c.title).set ("artist", c.artist).set ("key", c.key).set ("bpm", c.bpm).set ("time", c.time).set ("capo", c.capo);
    Json secs = Json::array();
    for (auto& s : c.sections) {
        Json js = Json::object(); js.set ("name", s.name).set ("label", s.label).set ("kind", s.kind).set ("color", s.color).set ("repeat", s.repeat).set ("bars", s.bars);
        if (! s.customColor.empty()) js.set ("customColor", s.customColor);
        Json items = Json::array();
        for (auto& it : s.items) {
            Json ji = Json::object();
            if (it.kind == ChartItem::Line) { ji.set ("lyric", it.line.lyric); if (! it.line.highlight.empty()) ji.set ("hl", it.line.highlight);
                Json ch = Json::array(); for (auto& c2 : it.line.chords) { Json x = Json::array(); x.push (c2.pos); x.push (c2.chord); ch.push (x); } ji.set ("chords", ch); }
            else if (it.kind == ChartItem::Tab) { Json t = Json::object(); t.set ("title", it.tab.title).set ("color", it.tab.color).set ("note", it.tab.note);
                Json st = Json::array(); for (auto& l : it.tab.strings) st.push (l); t.set ("strings", st); ji.set ("tab", t); }
            else ji.set ("comment", it.text);
            items.push (ji);
        }
        js.set ("items", items); secs.push (js);
    }
    j.set ("sections", secs);
    if (! c.arrangement.empty()) { Json a = Json::array(); for (auto& t : c.arrangement) a.push (t); j.set ("arrangement", a); }
    return j;
}
Chart chartFromJson (const Json& j) {
    Chart c; c.title = j["title"].str(); c.artist = j["artist"].str(); c.key = j["key"].str(); c.bpm = j["bpm"].num(); c.time = j["time"].str(); c.capo = j["capo"].integer (0);
    for (auto& js : j["sections"].items()) {
        Section s; s.name = js["name"].str(); s.label = js["label"].str(); s.kind = js["kind"].str(); s.color = js["color"].str(); s.repeat = js["repeat"].integer (1); s.bars = js["bars"].integer (0); s.customColor = normalizeColor (js["customColor"].str()); if (! s.customColor.empty()) s.color = s.customColor;
        for (auto& ji : js["items"].items()) {
            ChartItem it;
            if (ji.has ("tab")) { it.kind = ChartItem::Tab; auto& t = ji["tab"]; it.tab.title = t["title"].str(); it.tab.color = t["color"].str(); it.tab.note = t["note"].str(); for (auto& l : t["strings"].items()) it.tab.strings.push_back (l.str()); }
            else if (ji.has ("comment")) { it.kind = ChartItem::Comment; it.text = ji["comment"].str(); }
            else { it.line.lyric = ji["lyric"].str(); it.line.highlight = ji["hl"].str(); for (auto& x : ji["chords"].items()) it.line.chords.push_back ({ x[(size_t) 0].integer(), x[(size_t) 1].str() }); }
            s.items.push_back (it);
        }
        c.sections.push_back (s);
    }
    for (auto& t : j["arrangement"].items()) c.arrangement.push_back (t.str());
    return c;
}
Json toJson (const Song& s) {
    Json j = Json::object(); j.set ("id", s.id).set ("chart", toJson (s.chart)).set ("rig", s.rigPreset).set ("defaultScene", s.defaultScene);
    Json sc = Json::array();
    for (auto& x : s.scenes) { Json js = Json::object(); js.set ("name", x.name); Json p = Json::object(); for (auto& kv : x.pedals) { Json q = Json::object(); q.set ("on", kv.second.enabled).set ("params", params (kv.second.params)); p.set (kv.first, q); } js.set ("pedals", p); sc.push (js); }
    j.set ("scenes", sc);
    Json ss = Json::object(); for (auto& kv : s.sectionScene) ss.set (kv.first, kv.second); j.set ("sectionScene", ss);
    Json cu = Json::object(); for (auto& kv : s.cues) cu.set (kv.first, kv.second); j.set ("cues", cu);
    auto sw = [] (const SwitchAssign& a) { Json o = Json::object(); o.set ("label", a.label).set ("pedal", a.pedalUid); return o; };
    j.set ("switchC", sw (s.switchC)).set ("switchD", sw (s.switchD)).set ("click", s.clickDefault).set ("countIn", s.countIn);
    if (! s.ccli.number.empty() || ! s.ccli.title.empty() || ! s.ccli.authors.empty() || s.ccli.publicDomain)
        j.set ("ccli", Json::object().set ("number", s.ccli.number).set ("title", s.ccli.title).set ("authors", s.ccli.authors).set ("pd", s.ccli.publicDomain));
    if (! s.multitrack.empty() || ! s.multitrack.sectionBeats.empty()) j.set ("multitrack", mt::toJson (s.multitrack));
    if (! s.practice.empty()) { Json m = Json::array(); for (double x : s.practice.marks) m.push (x); j.set ("practice", Json::object().set ("file", s.practice.file).set ("marks", m)); }
    return j;
}
Song songFromJson (const Json& j) {
    Song s; s.id = j["id"].str(); s.chart = chartFromJson (j["chart"]); s.rigPreset = j["rig"].str ("Worship Ambient"); s.defaultScene = j["defaultScene"].str();
    for (auto& js : j["scenes"].items()) { Scene x; x.name = js["name"].str(); for (auto& kv : js["pedals"].members()) { PedalState ps; ps.enabled = kv.second["on"].boolean(); ps.params = paramsFrom (kv.second["params"]); x.pedals[kv.first] = ps; } s.scenes.push_back (x); }
    for (auto& kv : j["sectionScene"].members()) s.sectionScene[kv.first] = kv.second.str();
    for (auto& kv : j["cues"].members()) s.cues[kv.first] = kv.second.str();
    s.switchC = { j["switchC"]["label"].str(), j["switchC"]["pedal"].str() }; s.switchD = { j["switchD"]["label"].str(), j["switchD"]["pedal"].str() }; s.clickDefault = j["click"].boolean (false); s.countIn = j["countIn"].integer (-1);
    if (j.has ("ccli")) { const Json& c = j["ccli"]; s.ccli.number = c["number"].str(); s.ccli.title = c["title"].str(); s.ccli.authors = c["authors"].str(); s.ccli.publicDomain = c["pd"].boolean(); }
    if (j.has ("multitrack")) s.multitrack = mt::multitrackFromJson (j["multitrack"]);
    s.practice.file = j["practice"]["file"].str(); for (auto& x : j["practice"]["marks"].items()) s.practice.marks.push_back (x.num (-1));
    return s;
}
Json toJson (const RigPreset& r) {
    Json j = Json::object(); j.set ("name", r.name);
    const char* names[2] = { "pre", "post" };
    for (int l = 0; l < 2; ++l) { Json a = Json::array(); for (auto& p : r.lanes[l]) { Json o = Json::object(); o.set ("uid", p.uid).set ("type", p.type).set ("on", p.enabled).set ("params", params (p.params)); if (! p.file.empty()) o.set ("file", p.file); a.push (o); } j.set (names[l], a); }
    return j;
}
RigPreset rigFromJson (const Json& j) {
    RigPreset r; r.name = j["name"].str(); const char* names[2] = { "pre", "post" };
    for (int l = 0; l < 2; ++l) for (auto& o : j[names[l]].items()) { PedalDef d; d.uid = o["uid"].str(); d.type = o["type"].str(); d.enabled = o["on"].boolean(); d.params = paramsFrom (o["params"]); d.file = o["file"].str(); r.lanes[l].push_back (d); }
    return r;
}
Json toJson (const ProjectorSettings& s) {
    return Json::object().set ("lines", s.linesPerSlide).set ("title", s.titleSlide).set ("blank", s.blankBetweenSongs).set ("font", s.fontSize)
        .set ("upper", s.uppercase).set ("bg", s.background).set ("bgDim", s.backgroundDim).set ("logo", s.logo).set ("notes", s.showNotes);
}
ProjectorSettings projectorSettingsFromJson (const Json& j) {
    ProjectorSettings s; s.linesPerSlide = std::clamp (j["lines"].integer (2), 1, 8); s.titleSlide = j["title"].boolean (true);
    s.blankBetweenSongs = j["blank"].boolean (true); s.fontSize = std::clamp (j["font"].integer (2), 0, 3); s.uppercase = j["upper"].boolean (false);
    s.background = j["bg"].str(); s.backgroundDim = (float) j["bgDim"].num (0.45); s.logo = j["logo"].str(); s.showNotes = j["notes"].boolean (false); return s;
}

Json toJson (const Setlist& s) {
    Json j = Json::object(); j.set ("name", s.name).set ("date", s.date); Json a = Json::array();
    for (auto& e : s.entries) { Json o = Json::object(); o.set ("song", e.songId).set ("key", e.keyOverride).set ("bpm", e.bpmOverride).set ("role", e.role).set ("capo", e.capoOverride);
        if (! e.arrangement.empty()) { Json ar = Json::array(); for (auto& t : e.arrangement) ar.push (t); o.set ("arrangement", ar); }
        if (! e.notes.empty()) { Json ns = Json::array(); for (auto& n : e.notes) ns.push (Json::object().set ("text", n.text).set ("at", n.at)); o.set ("notes", ns); }
        a.push (o); }
    j.set ("entries", a);
    Json ps = Json::array();
    for (auto& p : s.players) { Json o = Json::object(); o.set ("name", p.name).set ("instrument", p.instrument).set ("capo", p.capo); Json m = Json::object();
        for (auto& kv : p.songs) { Json q = Json::object(); q.set ("key", kv.second.key).set ("capo", kv.second.capo); m.set (kv.first, q); } o.set ("songs", m); ps.push (o); }
    j.set ("players", ps); return j;
}
Setlist setlistFromJson (const Json& j) {
    Setlist s; s.name = j["name"].str(); s.date = j["date"].str();
    for (auto& o : j["entries"].items()) { SetlistEntry e; e.songId = o["song"].str(); e.keyOverride = o["key"].str(); e.bpmOverride = o["bpm"].num(); e.role = o["role"].str ("lead"); e.capoOverride = o["capo"].integer (-1);
        for (auto& t : o["arrangement"].items()) e.arrangement.push_back (t.str());
        for (auto& n : o["notes"].items()) e.notes.push_back ({ n["text"].str(), n["at"].str ("start") });
        s.entries.push_back (e); }
    for (auto& o : j["players"].items()) { Player p; p.name = o["name"].str(); p.instrument = o["instrument"].str(); p.capo = o["capo"].integer (0);
        for (auto& kv : o["songs"].members()) p.songs[kv.first] = { kv.second["key"].str(), kv.second["capo"].integer (-1) }; s.players.push_back (p); }
    return s;
}
static Json bind (const MidiBinding& b) { Json o = Json::object(); static const char* k[] = { "none", "cc", "pc", "note" }; o.set ("kind", k[(int) b.kind]).set ("num", b.number).set ("ch", b.channel); return o; }
static MidiBinding bindFrom (const Json& o) { MidiBinding b; std::string k = o["kind"].str ("none"); b.kind = k == "cc" ? MidiBinding::CC : k == "pc" ? MidiBinding::PC : k == "note" ? MidiBinding::Note : MidiBinding::None; b.number = o["num"].integer (-1); b.channel = o["ch"].integer (0); return b; }
Json toJson (const ControllerProfile& c) {
    Json j = Json::object(); j.set ("name", c.name).set ("role", c.role).set ("channel", c.channel).set ("longPressMs", c.longPressMs);
    Json a = Json::array(); for (auto& s : c.sw) { Json o = Json::object(); o.set ("press", bind (s.press)).set ("long", bind (s.longPress)).set ("action", s.action).set ("longAction", s.longAction).set ("longMs", s.longMs); a.push (o); }
    j.set ("switches", a);
    j.set ("comboE", c.comboE).set ("comboF", c.comboF);
    if (! c.banks.empty()) { Json bk = Json::array(); for (auto& b : c.banks) { Json o = Json::object(), x = Json::array(), y = Json::array(); for (int i = 0; i < 4; ++i) { x.push (b.action[i]); y.push (b.longAction[i]); }
        o.set ("name", b.name).set ("actions", x).set ("long", y); bk.push (o); } j.set ("banks", bk).set ("bank", c.bank); }
    return j;
}
ControllerProfile controllerFromJson (const Json& j) {
    ControllerProfile c; c.name = j["name"].str(); c.role = j["role"].str(); c.channel = j["channel"].integer (1); c.longPressMs = j["longPressMs"].integer (500);
    for (int i = 0; i < 4; ++i) { auto& o = j["switches"][(size_t) i]; c.sw[i].press = bindFrom (o["press"]); c.sw[i].longPress = bindFrom (o["long"]); c.sw[i].action = o["action"].str(); c.sw[i].longAction = o["longAction"].str(); c.sw[i].longMs = o["longMs"].integer (0); }
    for (auto& o : j["banks"].items()) { ControllerProfile::Bank b; b.name = o["name"].str(); for (int i = 0; i < 4; ++i) { b.action[i] = o["actions"][(size_t) i].str(); b.longAction[i] = o["long"][(size_t) i].str(); } c.banks.push_back (b); }
    c.bank = c.banks.empty() ? 0 : std::clamp (j["bank"].integer (0), 0, (int) c.banks.size() - 1);
    // Saved libraries still have section switch A as "previous section". That button is Play, hold = Stop.
    if (c.role == "section") {
        auto fix = [] (std::string& act, std::string& lng) { if (act == "prevSection" && lng.empty()) { act = "playStart"; lng = "stop"; } };
        fix (c.sw[0].action, c.sw[0].longAction);
        if (c.sw[0].longAction == "stop" && c.sw[0].longMs <= 0) c.sw[0].longMs = 700;
        for (auto& b : c.banks) fix (b.action[0], b.longAction[0]);
    }
    // Missing keys (libraries saved before E/F) get the factory combo. A stored empty string stays empty.
    if (j.has ("comboE")) c.comboE = j["comboE"].str();
    else c.comboE = c.role == "section" ? "prevSection" : "tunerToggle";
    if (j.has ("comboF")) c.comboF = j["comboF"].str();
    else c.comboF = c.role == "section" ? "stop" : "clickMute";
    return c;
}
Json toJson (const Library& l) {
    Json j = Json::object(); j.set ("version", 1);
    Json a = Json::array(); for (auto& s : l.songs) a.push (toJson (s)); j.set ("songs", a);
    Json b = Json::array(); for (auto& s : l.setlists) b.push (toJson (s)); j.set ("setlists", b);
    Json c = Json::array(); for (auto& r : l.rigs) c.push (toJson (r)); j.set ("rigs", c);
    Json d = Json::array(); for (auto& r : l.controllers) d.push (toJson (r)); j.set ("controllers", d);
    Json st = Json::object(); st.set ("routing", l.settings.routing).set ("trimPre", l.settings.trimPreDb).set ("trimPost", l.settings.trimPostDb).set ("autoReturn", l.settings.autoReturnSec)
        .set ("extCh", l.settings.externalChannel).set ("noteBase", l.settings.noteBase).set ("clockMode", l.settings.midiClockMode).set ("clock", l.settings.midiClockMode == 2)
        .set ("clockOut", l.settings.midiClockOutId).set ("clockIn", l.settings.midiClockInId)
        .set ("clickOut", l.settings.clickOut).set ("clickDb", l.settings.clickDb).set ("clickMuted", l.settings.clickMuted).set ("clickAccent", l.settings.clickAccent).set ("clickPreset", l.settings.clickPreset).set ("downbeatDb", l.settings.downbeatDb)
        .set ("tunerRef", l.settings.tunerRefHz).set ("autoAdvance", l.settings.autoAdvance).set ("rigTail", l.settings.rigTailSec)
        .set ("voiceCues", l.settings.voiceCues).set ("cueDb", l.settings.cueDb).set ("cuesWhenMuted", l.settings.cuesWhenClickMuted).set ("quantize", l.settings.quantizeSections)
        .set ("countIn", l.settings.countIn).set ("countInSpoken", l.settings.countInSpoken)
        .set ("deviceName", l.settings.deviceName).set ("leaderHost", l.settings.lastLeaderHost).set ("leaderPort", l.settings.lastLeaderPort).set ("link", l.settings.useLink).set ("projector", toJson (l.settings.projector));
    { Json sc = Json::object(); for (auto& kv : l.settings.sectionColors) sc.set (kv.first, kv.second); st.set ("sectionColors", sc); }
    st.set ("stageMode", l.settings.stageMode).set ("autoScroll", l.settings.autoScroll).set ("recordClick", l.settings.recordClick).set ("repeatWarnDays", l.settings.repeatWarnDays).set ("trackJumpAt", l.settings.trackJumpAt)
        .set ("oversampling", l.settings.ampOversampling).set ("osAuto", l.settings.autoOversampling)
        .set ("guitarPedal", l.settings.guitarPedalId).set ("sectionPedal", l.settings.sectionPedalId)
        .set ("guitarPedalName", l.settings.guitarPedalName).set ("sectionPedalName", l.settings.sectionPedalName);
    if (! l.settings.favoritePedals.empty()) { Json a = Json::array(); for (auto& s : l.settings.favoritePedals) a.push (s); st.set ("favPedals", a); }
    if (! l.settings.recentPedals.empty()) { Json a = Json::array(); for (auto& s : l.settings.recentPedals) a.push (s); st.set ("recentPedals", a); }
    { Json pl = Json::array(); for (auto& e : l.playLog) pl.push (Json::object().set ("song", e.songId).set ("date", e.date)); j.set ("playLog", pl); }
    { Json ul = Json::array(); for (auto& e : l.usageLog) ul.push (Json::object().set ("date", e.date).set ("song", e.songId).set ("title", e.title).set ("ccli", e.ccliNumber)
          .set ("authors", e.authors).set ("setlist", e.setlist).set ("pd", e.publicDomain).set ("projected", e.projected).set ("chart", e.chart)); j.set ("usageLog", ul); }
    j.set ("settings", st); return j;
}
Library libraryFromJson (const Json& j) {
    Library l; for (auto& s : j["songs"].items()) l.songs.push_back (songFromJson (s)); for (auto& s : j["setlists"].items()) l.setlists.push_back (setlistFromJson (s));
    for (auto& s : j["rigs"].items()) l.rigs.push_back (rigFromJson (s)); for (auto& s : j["controllers"].items()) l.controllers.push_back (controllerFromJson (s));
    auto& st = j["settings"]; l.settings.routing = st["routing"].integer (0); l.settings.trimPreDb = (float) st["trimPre"].num (-10); l.settings.trimPostDb = (float) st["trimPost"].num (0);
    l.settings.autoReturnSec = st["autoReturn"].integer (10); l.settings.externalChannel = st["extCh"].integer (16); l.settings.noteBase = st["noteBase"].integer (36);
    // v0.17: clockMode 0/1/2; older libraries only had boolean "clock" (true = Receive)
    if (st.has ("clockMode")) l.settings.midiClockMode = std::clamp (st["clockMode"].integer (0), 0, 2);
    else l.settings.midiClockMode = st["clock"].boolean (false) ? 2 : 0;
    l.settings.followMidiClock = l.settings.midiClockMode == 2;
    l.settings.midiClockOutId = st["clockOut"].str(); l.settings.midiClockInId = st["clockIn"].str();
    l.settings.clickOut = st["clickOut"].integer (2); l.settings.clickDb = (float) st["clickDb"].num (-12); l.settings.clickMuted = st["clickMuted"].boolean (false); l.settings.clickAccent = st["clickAccent"].boolean (true); l.settings.clickPreset = st["clickPreset"].integer (0); l.settings.downbeatDb = (float) st["downbeatDb"].num (4.4);
    l.settings.tunerRefHz = (float) st["tunerRef"].num (440); l.settings.autoAdvance = st["autoAdvance"].boolean (true); l.settings.rigTailSec = (float) st["rigTail"].num (3);
    l.settings.voiceCues = st["voiceCues"].boolean (true); l.settings.cueDb = (float) st["cueDb"].num (-6); l.settings.cuesWhenClickMuted = st["cuesWhenMuted"].boolean (false);
    l.settings.quantizeSections = st["quantize"].boolean (true); l.settings.countIn = st["countIn"].integer (8); l.settings.countInSpoken = st["countInSpoken"].boolean (false);
    l.settings.deviceName = st["deviceName"].str ("Bearing Live"); l.settings.lastLeaderHost = st["leaderHost"].str(); l.settings.lastLeaderPort = st["leaderPort"].integer(); l.settings.useLink = st["link"].boolean (false);
    l.settings.projector = projectorSettingsFromJson (st["projector"]);
    { int o = st["oversampling"].integer (4); l.settings.ampOversampling = o >= 8 ? 8 : o >= 4 ? 4 : o >= 2 ? 2 : 1; l.settings.autoOversampling = st["osAuto"].boolean (true); }
    l.settings.guitarPedalId = st["guitarPedal"].str(); l.settings.sectionPedalId = st["sectionPedal"].str();
    l.settings.guitarPedalName = st["guitarPedalName"].str(); l.settings.sectionPedalName = st["sectionPedalName"].str();
    for (auto& e : st["favPedals"].items()) if (! e.str().empty()) l.settings.favoritePedals.push_back (e.str());
    for (auto& e : st["recentPedals"].items()) if (! e.str().empty()) l.settings.recentPedals.push_back (e.str());
    l.settings.stageMode = false; // saved "dark stage mode" is ignored; the app has one dark look
    l.settings.autoScroll = st["autoScroll"].boolean (true); l.settings.recordClick = st["recordClick"].boolean (false); l.settings.repeatWarnDays = st["repeatWarnDays"].integer (21); l.settings.trackJumpAt = std::clamp (st["trackJumpAt"].integer (0), 0, 1);
    for (auto& e : j["playLog"].items()) l.playLog.push_back ({ e["song"].str(), e["date"].str() });
    for (auto& e : j["usageLog"].items()) { UsageEntry u; u.date = e["date"].str(); u.songId = e["song"].str(); u.title = e["title"].str(); u.ccliNumber = e["ccli"].str();
        u.authors = e["authors"].str(); u.setlist = e["setlist"].str(); u.publicDomain = e["pd"].boolean(); u.projected = e["projected"].boolean(); u.chart = e["chart"].boolean(); l.usageLog.push_back (u); }
    for (auto& kv : st["sectionColors"].members()) { auto n = normalizeColor (kv.second.str()); if (! n.empty()) l.settings.sectionColors[kv.first] = n; }
    return l;
}

// ------------------------------------------------------------------ defaults
// Chocolate Plus: set each switch to momentary CC (127 on press, 0 on release) in the M-Vave app so the rig can see long presses.
static ControllerProfile chocolate (const std::string& name, const std::string& role, int ch, const char* a[4], const char* la[4]) {
    ControllerProfile c; c.name = name; c.role = role; c.channel = ch;
    for (int i = 0; i < 4; ++i) { c.sw[i].press.kind = MidiBinding::CC; c.sw[i].press.number = 80 + i; c.sw[i].action = a[i]; c.sw[i].longAction = la[i]; }
    if (role == "section") { c.comboE = "prevSection"; c.comboF = "stop"; }
    else { c.comboE = "tunerToggle"; c.comboF = "clickMute"; }
    return c;
}
void enableDefaultBanks (ControllerProfile& c) {
    if (! c.banks.empty()) return; ControllerProfile::Bank a; a.name = c.role == "guitar" ? "Rig" : "Sections";
    for (int i = 0; i < 4; ++i) { a.action[i] = c.sw[i].action; a.longAction[i] = c.sw[i].longAction; }
    ControllerProfile::Bank b;
    if (c.role == "guitar") { b.name = "Click / transport"; const char* x[4] = { "tap", "clickToggle", "play", "toggleView" }; const char* y[4] = { "tunerToggle", "bankNext", "", "clickMute" };
        for (int i = 0; i < 4; ++i) { b.action[i] = x[i]; b.longAction[i] = y[i]; } a.longAction[1] = "bankNext"; }
    else { b.name = "Songs"; const char* x[4] = { "prevSong", "nextSong", "play", "toggleView" }; const char* y[4] = { "", "", "bankNext", "" };
        for (int i = 0; i < 4; ++i) { b.action[i] = x[i]; b.longAction[i] = y[i]; } a.longAction[2] = "bankNext"; }
    c.banks = { a, b }; c.bank = 0;
}
ControllerProfile defaultSectionUnit() {
    const char* a[4] = { "playStart", "nextSection", "jumpChorus", "nextSong" }; const char* la[4] = { "stop", "", "jumpChorus", "prevSong" };
    auto c = chocolate ("Chocolate Plus - Sections", "section", 2, a, la); c.sw[0].longMs = 700; // tap = start, hold ~0.7 s = stop (release does not also start)
    return c;
}
ControllerProfile defaultGuitarUnit() { const char* a[4] = { "tap", "boost", "switchC", "switchD" }; const char* la[4] = { "tunerToggle", "", "", "clickMute" };
    // A: tap fires on press (tempo-accurate), hold 1 s opens the tuner. D: short = per-song D (on release), hold 0.6 s = click mute.
    auto c = chocolate ("Chocolate Plus - Guitar", "guitar", 1, a, la); c.sw[0].longMs = 1000; c.sw[3].longMs = 600; return c; }
void resetFootswitchMap (ControllerProfile& c) {
    auto d = c.role == "section" ? defaultSectionUnit() : defaultGuitarUnit();
    for (int i = 0; i < 4; ++i) {
        c.sw[i].action = d.sw[i].action; c.sw[i].longAction = d.sw[i].longAction; c.sw[i].longMs = d.sw[i].longMs;
        if (! c.banks.empty()) { auto bi = (size_t) std::clamp (c.bank, 0, (int) c.banks.size() - 1);
            c.banks[bi].action[i] = d.sw[i].action; c.banks[bi].longAction[i] = d.sw[i].longAction; }
    }
    c.comboE = d.comboE; c.comboF = d.comboF;
}

RigPreset defaultRig() {
    RigPreset r; r.name = "Worship Ambient";
    auto add = [&] (int lane, const char* uid, const char* type, bool on, std::map<std::string, float> p = {}) { r.lanes[lane].push_back ({ uid, type, on, std::move (p) }); };
    add (0, "comp", "comp", true, { { "sustain", 0.4f }, { "blend", 0.7f } });
    add (0, "boost", "boost", false, { { "gain", 6.0f } });
    add (0, "od", "overdrive", false, { { "drive", 0.35f }, { "tone", 0.55f } });
    add (0, "fuzz", "fuzz", false);
    add (0, "oct", "octdown", false, { { "sub1", 0.6f }, { "dry", 0.8f } });
    add (1, "eq", "eq", true, { { "lowcut", 90.0f } });
    add (1, "chorus", "chorus", false, { { "mix", 0.35f } });
    add (1, "trem", "tremolo", false, { { "sync", 2.0f }, { "depth", 0.5f } });
    add (1, "delay", "delay", true, { { "div", 1.0f }, { "mix", 0.25f }, { "feedback", 0.35f } });
    add (1, "verb", "reverb", true, { { "mix", 0.25f }, { "decay", 3.5f } });
    add (1, "shim", "shimmer", false, { { "mix", 0.4f } });
    add (1, "freeze", "freeze", false);
    return r;
}

Library seedLibrary() {
    Library l; l.rigs.push_back (defaultRig()); l.controllers = { defaultGuitarUnit(), defaultSectionUnit() };
    Song s; s.id = "he-who-was"; s.chart = parseChart (seedChartText(), "He who was / He who is"); s.rigPreset = "Worship Ambient";
    auto st = [] (bool on, std::map<std::string, float> p = {}) { PedalState x; x.enabled = on; x.params = std::move (p); return x; };
    Scene verse { "Verse", { { "od", st (false) }, { "shim", st (false) }, { "delay", st (true, { { "mix", 0.22f } }) }, { "verb", st (true) }, { "chorus", st (false) } } };
    Scene pre { "Pre-Chorus", { { "od", st (true, { { "drive", 0.3f } }) }, { "shim", st (false) }, { "delay", st (true, { { "mix", 0.3f } }) } } };
    Scene chorus { "Chorus", { { "od", st (true, { { "drive", 0.45f } }) }, { "shim", st (true) }, { "delay", st (true, { { "mix", 0.3f } }) } } };
    s.scenes = { verse, pre, chorus }; s.defaultScene = "Verse";
    s.sectionScene = { { "verse", "Verse" }, { "prechorus", "Pre-Chorus" }, { "chorus", "Chorus" } };
    s.switchC = { "Octave", "oct" }; s.switchD = { "Freeze", "freeze" };
    s.cues = { { "Verse 1", "Orange riff" }, { "Pre-Chorus", "Pink riff" }, { "Final Chorus", "Hit D: Freeze on the last chord" } };
    l.songs.push_back (s);
    Setlist sl; sl.name = "Sunday"; sl.date = "2026-11-08"; sl.entries.push_back ({ s.id, "", 0, "lead" }); l.setlists.push_back (sl);
    return l;
}

} // namespace wr

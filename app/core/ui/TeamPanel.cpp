#include "TeamPanel.h"
#include "SeatSession.h"
#include "Platform.h"
#include "Theme.h"

TeamPanel::TeamPanel (SeatSession& s) : seats_ (s) {
    for (auto* b : { &signIn_, &join_, &create_, &invite_, &revoke_, &remove_, &leave_, &delete_, &close_ }) addAndMakeVisible (*b);
    for (auto* t : { &code_, &name_, &church_ }) { addAndMakeVisible (*t); t->setFont (juce::FontOptions (20.0f)); }
    code_.setTextToShowWhenEmpty ("Invite code  (e.g. 7KQ2M-9XD4P)", juce::Colours::grey); code_.setInputRestrictions (16, "0123456789ABCDEFGHJKMNPQRSTVWXYZabcdefghjkmnpqrstvwxyzOoIiLl- ");
    name_.setTextToShowWhenEmpty ("Your name on the team (optional)", juce::Colours::grey); name_.setInputRestrictions (40);
    church_.setTextToShowWhenEmpty ("Church name", juce::Colours::grey); church_.setInputRestrictions (80);
    addAndMakeVisible (list_); list_.setRowHeight (40);
    close_.onClick = [this] { setVisible (false); };
    signIn_.onClick = [this] {
        busy ("Signing in with Apple...");
        wr::platform::signInWithApple ([this] (juce::String idToken, juce::String err) {
            if (idToken.isEmpty()) { working_ = false; status_ = err.isEmpty() ? "Sign in cancelled" : err; layout(); return; }
            auto t = idToken.toStdString();
            seats_.async ([t] (wr::SeatClient& c) { return c.signInWithApple (t); }, [this] (wr::SeatResult r) { report (r, "Signed in"); if (r.ok) reload(); });
        });
    };
    join_.onClick = [this] {
        auto code = code_.getText().toStdString(), name = name_.getText().trim().toStdString();
        if (wr::SeatClient::normaliseCode (code).size() != 10) { status_ = "Invite codes are 10 letters/numbers"; layout(); return; }
        busy ("Joining...");
        seats_.async ([code, name] (wr::SeatClient& c) { return c.redeem (code, name); },
                      [this] (wr::SeatResult r) { report (r, "You're on the team - full access unlocked"); if (r.ok) { code_.clear(); seats_.refreshEntitlement (true); reload(); } });
    };
    create_.onClick = [this] {
        auto church = church_.getText().trim(); if (church.isEmpty()) { status_ = "Enter your church name"; layout(); return; }
        busy ("Checking your Church subscription...");
        wr::platform::churchSubscriptionJws ([this, church] (juce::String jws) {
            if (jws.isEmpty()) { working_ = false; status_ = "Subscribe to the Church plan first (Settings > Subscription)"; layout(); return; }
            auto c = church.toStdString(), n = name_.getText().trim().toStdString(), j = jws.toStdString();
            seats_.async ([c, n, j] (wr::SeatClient& cl) { return cl.createTeam (c, n, j); },
                          [this] (wr::SeatResult r) { report (r, "Team created - now make invite codes for your band"); if (r.ok) { seats_.refreshEntitlement (true); reload(); } });
        });
    };
    invite_.onClick = [this] {
        busy ("Creating invite...");
        auto code = std::make_shared<std::string>();
        seats_.async ([code] (wr::SeatClient& c) { return c.createInvite (code.get()); }, [this, code] (wr::SeatResult r) {
            if (r.ok) { lastCode_ = *code; juce::SystemClipboard::copyTextToClipboard (lastCode_); }
            report (r, "Invite " + juce::String (*code) + " copied - single use, expires in 7 days"); reload(); });
    };
    revoke_.onClick = [this] {
        if (lastCode_.isEmpty()) return; auto code = lastCode_.toStdString(); busy ("Revoking...");
        seats_.async ([code] (wr::SeatClient& c) { return c.revokeInvite (code); }, [this] (wr::SeatResult r) { if (r.ok) lastCode_ = {}; report (r, "Code revoked"); reload(); });
    };
    remove_.onClick = [this] {
        int row = list_.getSelectedRow(); if (row < 0 || row >= (int) info_.members.size() || info_.members[(size_t) row].role == "leader") return;
        auto m = info_.members[(size_t) row];
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Remove " + juce::String (m.displayName) + "?", "They lose full access the next time their app checks in (at most 30 days offline).", "Remove", "Cancel", this,
            juce::ModalCallbackFunction::create ([this, id = m.id] (int ok) { if (! ok) return; busy ("Removing...");
                seats_.async ([id] (wr::SeatClient& c) { return c.removeMember (id); }, [this] (wr::SeatResult r) { report (r, "Removed"); reload(); }); }));
    };
    leave_.onClick = [this] {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Leave the team?", "Your charts stay; the app becomes read-only unless you subscribe yourself.", "Leave", "Cancel", this,
            juce::ModalCallbackFunction::create ([this] (int ok) { if (! ok) return; busy ("Leaving...");
                seats_.async ([] (wr::SeatClient& c) { return c.leave(); }, [this] (wr::SeatResult r) { report (r, "You left the team"); seats_.refreshEntitlement (true); reload(); }); }));
    };
    delete_.onClick = [this] {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Delete your seat account?",
            "Deletes your account on the Tabernacle Tools seat server. If you lead a team, the team is dissolved and everyone loses their seat. "
            "Your Apple subscription is NOT cancelled here - do that in iOS Settings > Apple ID > Subscriptions. Your charts on this iPad are kept.", "Delete", "Cancel", this,
            juce::ModalCallbackFunction::create ([this] (int ok) { if (! ok) return; busy ("Deleting...");
                seats_.async ([] (wr::SeatClient& c) { return c.deleteAccount(); }, [this] (wr::SeatResult r) { report (r, "Account deleted"); hasTeam_ = false; info_ = {}; seats_.refreshEntitlement (true); layout(); }); }));
    };
}
juce::String TeamPanel::friendly (const std::string& e) {
    static const std::map<std::string, const char*> m = {
        { "network", "Can't reach the server - check Wi-Fi. Your seat keeps working offline for up to 30 days." },
        { "invalid_or_expired_code", "That code is wrong, already used, or expired. Ask your leader for a new one." },
        { "team_full", "This team already has 15 people (including the leader)." },
        { "already_in_team", "You're already on a team. Leave it first." },
        { "rate_limited", "Too many tries - wait a bit and try again." },
        { "subscription_inactive", "The church subscription isn't active. The leader can renew it in iOS Settings." },
        { "not_church_plan", "That purchase is the Individual plan; teams need the Church plan." },
        { "subscription_already_has_team", "That Church subscription already has a team." },
        { "leader_cannot_leave", "Leaders can't leave their own team - delete the account to dissolve it." },
        { "no_team", "You're not on a team yet." }, { "not_signed_in", "Sign in with Apple first." } };
    auto it = m.find (e); return it != m.end() ? juce::String (it->second) : "Something went wrong (" + juce::String (e) + ")";
}
void TeamPanel::report (const wr::SeatResult& r, const juce::String& okMsg) { working_ = false; status_ = r.ok ? okMsg : friendly (r.error); layout(); }
void TeamPanel::open() { setVisible (true); toFront (true); reload(); }
void TeamPanel::reload() {
    if (! seats_.client().signedIn()) { hasTeam_ = false; layout(); return; }
    auto info = std::make_shared<wr::SeatTeamInfo>();
    seats_.async ([info] (wr::SeatClient& c) { return c.myTeam (info.get()); }, [this, info] (wr::SeatResult r) {
        hasTeam_ = r.ok; info_ = r.ok ? *info : wr::SeatTeamInfo {}; if (! r.ok && r.error != "no_team") status_ = friendly (r.error);
        list_.updateContent(); layout(); });
}
void TeamPanel::layout() {
    bool in = seats_.client().signedIn(), leader = hasTeam_ && info_.role == "leader";
    signIn_.setVisible (! in);
    for (auto* c : std::initializer_list<juce::Component*> { &code_, &join_ }) c->setVisible (in && ! hasTeam_);
    for (auto* c : std::initializer_list<juce::Component*> { &church_, &create_ }) c->setVisible (in && ! hasTeam_);
    name_.setVisible (in && ! hasTeam_);
    for (auto* c : std::initializer_list<juce::Component*> { &invite_, &remove_ }) c->setVisible (leader);
    revoke_.setVisible (leader && lastCode_.isNotEmpty());
    list_.setVisible (hasTeam_); leave_.setVisible (hasTeam_ && ! leader); delete_.setVisible (in);
    invite_.setEnabled (! working_ && (int) info_.members.size() < info_.seatCap);
    for (auto* b : { &signIn_, &join_, &create_, &remove_, &leave_, &delete_ }) b->setEnabled (! working_);
    resized(); repaint();
}
void TeamPanel::paint (juce::Graphics& g) {
    g.fillAll (juce::Colours::black.withAlpha (0.6f));
    g.setColour (juce::Colour (0xff1c1f26)); g.fillRoundedRectangle (box_.toFloat(), 14.0f);
    auto r = box_.reduced (24); g.setColour (juce::Colours::white); g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText (hasTeam_ ? juce::String (info_.churchName) + " - church team" : "Join your church team", r.removeFromTop (36), juce::Justification::left);
    g.setFont (juce::FontOptions (16.0f)); g.setColour (juce::Colours::lightgrey);
    juce::String sub;
    if (hasTeam_) {
        sub << (int) info_.members.size() << " of " << info_.seatCap << " seats used (including the leader)";
        if (info_.paidThrough > 0) sub << "  -  paid through " << juce::Time (info_.paidThrough * 1000).formatted ("%d %b %Y");
        if (info_.status == "grace") sub << "  -  payment issue: the leader should update billing";
        if (info_.role == "leader" && info_.openInvites > 0) sub << "  -  " << info_.openInvites << " unused invite code(s)";
    } else sub = "A church leader with the Church plan ($19.99/mo, 15 people) can invite you. Enter the code they gave you.";
    g.drawFittedText (sub, r.removeFromTop (44), juce::Justification::topLeft, 2);
    if (status_.isNotEmpty()) { g.setColour (juce::Colour (0xffffd166)); g.drawFittedText (status_, box_.reduced (24).removeFromBottom (110).removeFromTop (40), juce::Justification::left, 2); }
    g.setColour (juce::Colours::grey); g.setFont (juce::FontOptions (13.0f));
    g.drawFittedText ("Privacy: the seat server stores a hashed Apple ID, your optional display name and your team. No email.", box_.reduced (24).removeFromBottom (22), juce::Justification::left, 1);
}
void TeamPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) {
    if (row < 0 || row >= (int) info_.members.size()) return; auto& m = info_.members[(size_t) row];
    if (sel) g.fillAll (juce::Colour (0xff2e3440));
    g.setColour (juce::Colours::white); g.setFont (juce::FontOptions (18.0f));
    g.drawText (juce::String (m.displayName) + (m.you ? "  (you)" : ""), 12, 0, w - 140, h, juce::Justification::left);
    g.setColour (m.role == "leader" ? juce::Colour (0xffffd166) : juce::Colours::grey); g.drawText (m.role == "leader" ? "LEADER" : "member", w - 130, 0, 120, h, juce::Justification::right);
}
void TeamPanel::resized() {
    box_ = getLocalBounds().withSizeKeepingCentre (juce::jmin (720, getWidth() - 40), juce::jmin (640, getHeight() - 40));
    auto r = box_.reduced (24); r.removeFromTop (88); r.removeFromBottom (24);
    auto bottom = r.removeFromBottom (48); close_.setBounds (bottom.removeFromRight (120)); bottom.removeFromRight (8);
    if (delete_.isVisible()) { delete_.setBounds (bottom.removeFromRight (180)); bottom.removeFromRight (8); }
    if (leave_.isVisible()) leave_.setBounds (bottom.removeFromRight (150));
    r.removeFromBottom (56);   // status line
    if (signIn_.isVisible()) { signIn_.setBounds (r.removeFromTop (56).withSizeKeepingCentre (320, 52)); return; }
    if (! hasTeam_) {
        name_.setBounds (r.removeFromTop (44)); r.removeFromTop (12);
        auto row = r.removeFromTop (48); join_.setBounds (row.removeFromRight (160)); row.removeFromRight (8); code_.setBounds (row);
        r.removeFromTop (28);
        auto row2 = r.removeFromTop (48); create_.setBounds (row2.removeFromRight (220)); row2.removeFromRight (8); church_.setBounds (row2);
        return;
    }
    if (invite_.isVisible()) { auto row = r.removeFromTop (48); invite_.setBounds (row.removeFromLeft (200)); row.removeFromLeft (8);
        if (revoke_.isVisible()) { revoke_.setBounds (row.removeFromLeft (200)); row.removeFromLeft (8); } remove_.setBounds (row.removeFromLeft (200)); r.removeFromTop (10); }
    list_.setBounds (r);
}

// v0.11 church team panel (App Store builds). Member: sign in with Apple, enter an invite code. Leader: create the team
// from the Church subscription, see members (x/15), create / revoke invite codes, remove members. Everyone: leave, delete account.
#pragma once
#include <JuceHeader.h>
#include "sync/SeatClient.h"
class SeatSession;

class TeamPanel : public juce::Component, private juce::ListBoxModel {
public:
    explicit TeamPanel (SeatSession& s);
    void open();
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    int getNumRows() override { return (int) info_.members.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void reload();
    void busy (const juce::String& msg) { status_ = msg; working_ = true; layout(); }
    void report (const wr::SeatResult& r, const juce::String& okMsg);
    void layout();
    static juce::String friendly (const std::string& code);
    SeatSession& seats_; wr::SeatTeamInfo info_; bool hasTeam_ = false, working_ = false; juce::String status_, lastCode_;
    juce::TextEditor code_, name_, church_;
    juce::TextButton signIn_ { "SIGN IN WITH APPLE" }, join_ { "JOIN TEAM" }, create_ { "CREATE CHURCH TEAM" }, invite_ { "NEW INVITE CODE" },
                     revoke_ { "REVOKE LAST CODE" }, remove_ { "REMOVE SELECTED" }, leave_ { "LEAVE TEAM" }, delete_ { "DELETE ACCOUNT" }, close_ { "CLOSE" };
    juce::ListBox list_ { "members", this };
    juce::Rectangle<int> box_;
};

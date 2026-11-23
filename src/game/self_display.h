// Headless PlayerDisplay implementations (self_env.h): they keep or log the messages the player shows.
#pragma once
#include "game/self_env.h"
#include <functional>
#include <string>
#include <vector>

namespace biosys::game {

// Keeps every message shown (not the redraws of the current one) as {stringId, text}.
class RecordingPlayerDisplay : public PlayerDisplay {
public:
    struct Message {
        int stringId = 0;
        std::string text;
    };
    void showMessage(int stringId, const std::string& text) override { messages_.push_back({stringId, text}); }
    void redrawMessage(int, const std::string&) override {}
    const std::vector<Message>& messages() const { return messages_; }
    void clear() { messages_.clear(); }
    // The messages since the last take() / clear(); empties the list.
    std::vector<Message> take() {
        std::vector<Message> out;
        out.swap(messages_);
        return out;
    }
private:
    std::vector<Message> messages_;
};

// Writes each message as one line "player-message <stringId> <text>" to the sink (the --log-events output); newlines in
// the text are written as \n.
class LoggingPlayerDisplay : public PlayerDisplay {
public:
    using Sink = std::function<void(const std::string&)>;
    explicit LoggingPlayerDisplay(Sink sink) : sink_(std::move(sink)) {}
    void showMessage(int stringId, const std::string& text) override;
    void redrawMessage(int, const std::string&) override {}
private:
    Sink sink_;
};

} // namespace biosys::game

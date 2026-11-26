#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <vector>

// Chat with the Antigravity CLI (agy) from inside the plugin.
// Every message runs `agy --output-format stream-json [--conversation id] -p "<text>"`
// in the MixSight install folder (where MixSightBridge.exe and GEMINI.md are).
// agy starts the bridge itself, so the model sees the same plugin tools
// (status, sections, capture_audio, post_insight…) as in the terminal.
class AiChat
{
public:
    struct Message
    {
        enum class Role { user, ai, tool, error, info };
        Role role;
        juce::String text;
    };

    AiChat();
    ~AiChat();

    bool send (const juce::String& text, const juce::String& measurements = {}, const juce::String& genre = {});   // false if busy
    void cancel();
    void newConversation();

    bool isBusy() const;
    juce::String getStatus() const;
    std::vector<Message> getMessages() const;
    int getVersion() const noexcept { return version.load(); }   // bumps on every change
    juce::String getConversationId() const;

    // ---- setup (message thread) ----
    enum class SetupStep { ready, installAgy, connect, allowTools, signIn };
    void refreshSetup();
    SetupStep getSetupStep() const;                  // next thing the user has to do
    juce::File getAgyExecutable() const   { return agyExe; }
    juce::File getWorkspace() const       { return workspace; }
    juce::String getServerName() const    { return serverName; }
    juce::String getSetupProblem() const;            // empty when agy and the bridge are ready
    bool toolsAllowed() const;                       // permissions.allow has mcp(<server>/*)
    bool allowTools (juce::String& error);           // adds the rule to agy's settings.json
    bool connectBridge (juce::String& log);          // agy mcp add mixsight <bridge>
    bool openSignIn (juce::String& error);           // visible agy window for Google sign-in
    bool needsSignIn() const;                        // after an auth error, or no stored sign-in (Windows)
    bool isSigningIn() const noexcept     { return signInThread != nullptr && signInThread->isThreadRunning(); }

    static juce::File agyHome();                     // ~/.gemini
    static juce::String cleanMarkdown (const juce::String&);
    static juce::String describeTool (const juce::String& name, const juce::var& parameters);

    // stream-json parsing (public for tests)
    struct TurnState
    {
        juce::String responseText, conversationId, status, error;
        std::map<int, juce::String> stepText;
        std::set<int> toolStepsSeen;
        int lastToolStep = -1;
        bool sawJson = false, gotResult = false;
    };
    void handleLine (TurnState&, const juce::String& line);

private:
    class Worker;
    void runTurn (const juce::String& prompt);
    void addMessage (Message::Role, const juce::String&);
    void setLastAiText (const juce::String&);
    void setStatus (const juce::String&);

    mutable juce::CriticalSection lock;
    std::vector<Message> messages;
    juce::String status, conversationId;
    int aiMessageIndex = -1;                         // message being streamed in this turn
    std::atomic<int> version { 0 };

    juce::File agyExe, workspace, bridgeExe;
    juce::String serverName { "mixsight" };
    bool registered = false;
    std::atomic<bool> signInNeeded { false };

    std::unique_ptr<Worker> worker;
    std::unique_ptr<juce::Thread> signInThread;
    std::atomic<bool> signedInCached { true };

    JUCE_DECLARE_NON_COPYABLE (AiChat)
};

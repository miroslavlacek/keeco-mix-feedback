#pragma once
#include <juce_events/juce_events.h>
#include <functional>
#include "AppPaths.h"

// Local link between the plugin and the MCP bridge (MixSightBridge.exe).
// The plugin listens on 127.0.0.1 only; every message is one JSON request or reply.
namespace kmflink
{
constexpr int basePort = 47321;
constexpr int portCount = 10;
constexpr juce::uint32 magic = 0x4b4d4662;   // "KMFb"

inline juce::File portFile()
{
    return mixsight::appData().getChildFile ("link-port.txt");
}

inline juce::MemoryBlock toBlock (const juce::String& s)
{
    const auto utf8 = s.toUTF8();
    return { utf8.getAddress(), utf8.sizeInBytes() - 1 };
}

inline juce::String fromBlock (const juce::MemoryBlock& b)
{
    return juce::String::fromUTF8 (static_cast<const char*> (b.getData()), (int) b.getSize());
}
} // namespace kmflink

class McpLinkServer : private juce::InterprocessConnectionServer
{
public:
    using Handler = std::function<juce::String (const juce::String& requestJson)>;

    explicit McpLinkServer (Handler h) : handler (std::move (h))
    {
        for (int p = kmflink::basePort; p < kmflink::basePort + kmflink::portCount; ++p)
            if (beginWaitingForSocket (p, "127.0.0.1"))
            {
                port = p;
                kmflink::portFile().replaceWithText (juce::String (p));
                break;
            }
    }

    ~McpLinkServer() override
    {
        stop();
        const juce::ScopedLock sl (lock);
        connections.clear();
        if (port > 0 && kmflink::portFile().loadFileAsString().trim() == juce::String (port))
            kmflink::portFile().deleteFile();
    }

    int getPort() const noexcept { return port; }
    int getRequestCount() const noexcept { return requests.load(); }

private:
    struct Connection : juce::InterprocessConnection
    {
        Connection (McpLinkServer& o) : InterprocessConnection (false, kmflink::magic), owner (o) {}
        ~Connection() override { disconnect(); }
        void connectionMade() override {}
        void connectionLost() override { dead = true; }
        void messageReceived (const juce::MemoryBlock& m) override
        {
            ++owner.requests;
            const auto reply = owner.handler (kmflink::fromBlock (m));
            sendMessage (kmflink::toBlock (reply));
        }
        McpLinkServer& owner;
        std::atomic<bool> dead { false };
    };

    juce::InterprocessConnection* createConnectionObject() override
    {
        const juce::ScopedLock sl (lock);
        for (int i = connections.size(); --i >= 0;)        // drop finished connections
            if (connections[i]->dead.load()) connections.remove (i);
        return connections.add (new Connection (*this));
    }

    Handler handler;
    juce::CriticalSection lock;
    juce::OwnedArray<Connection> connections;
    int port = 0;
    std::atomic<int> requests { 0 };
};

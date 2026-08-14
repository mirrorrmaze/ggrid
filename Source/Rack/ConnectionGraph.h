#pragma once

#include "../Params/Identifiers.h"
#include <array>

namespace GGrid
{
    // A directed edge from one slot's output to another slot's input.
    struct Connection { int from = -1; int to = -1; };
    static constexpr int kMaxConnections = kMaxSlots * 2;

    // Pure topology helper -- no audio/JUCE dependency beyond kMaxSlots, deliberately kept
    // separate from GGridAudioProcessor's buffer-summing loop so the graph algorithm itself
    // (topological order, roots, sinks, the cycle fallback) can be exercised directly by the
    // offline test harness without needing to construct a full AudioProcessor.
    struct ConnectionGraphResult
    {
        std::array<int, kMaxSlots> order {};   // topological processing order; first orderCount entries valid
        int orderCount = 0;
        std::array<bool, kMaxSlots> isRoot {}; // active, no active predecessor -- seed with raw input
        std::array<bool, kMaxSlots> isSink {}; // active, no active successor -- sum into the final mix
        std::array<int, kMaxSlots> outDegree {};
        std::array<int, kMaxSlots> inDegree {};
    };

    // Kahn's algorithm over at most kMaxSlots nodes/kMaxConnections edges -- fixed-size, no heap
    // allocation, safe to call from the audio thread. Edges where either endpoint isn't active
    // are ignored entirely (as if they didn't exist). If the graph contains a cycle (shouldn't
    // happen -- connect-time validation rejects them -- but this function doesn't assume that),
    // whatever's left unreached after the main pass is appended in slot order at the end, so no
    // active node is ever silently dropped from the order.
    inline ConnectionGraphResult buildProcessingOrder (
        const std::array<Connection, kMaxConnections>& connections, int numConnections,
        const std::array<bool, kMaxSlots>& active)
    {
        ConnectionGraphResult result;

        for (int i = 0; i < numConnections; ++i)
        {
            const auto& c = connections[(size_t) i];
            if (active[(size_t) c.from] && active[(size_t) c.to])
            {
                ++result.outDegree[(size_t) c.from];
                ++result.inDegree[(size_t) c.to];
            }
        }

        std::array<int, kMaxSlots> remainingIn = result.inDegree;
        for (int i = 0; i < kMaxSlots; ++i)
            if (active[(size_t) i] && remainingIn[(size_t) i] == 0)
                result.order[(size_t) result.orderCount++] = i;

        std::array<bool, kMaxSlots> processed {};
        int head = 0;
        while (head < result.orderCount)
        {
            const int i = result.order[(size_t) head++];
            processed[(size_t) i] = true;

            for (int c = 0; c < numConnections; ++c)
            {
                if (connections[(size_t) c].from != i) continue;
                const int to = connections[(size_t) c].to;
                if (! active[(size_t) to]) continue;
                if (--remainingIn[(size_t) to] == 0)
                    result.order[(size_t) result.orderCount++] = to;
            }
        }

        for (int i = 0; i < kMaxSlots; ++i)
            if (active[(size_t) i] && ! processed[(size_t) i])
                result.order[(size_t) result.orderCount++] = i;

        for (int i = 0; i < kMaxSlots; ++i)
        {
            result.isRoot[(size_t) i] = active[(size_t) i] && result.inDegree[(size_t) i] == 0;
            result.isSink[(size_t) i] = active[(size_t) i] && result.outDegree[(size_t) i] == 0;
        }

        return result;
    }
}

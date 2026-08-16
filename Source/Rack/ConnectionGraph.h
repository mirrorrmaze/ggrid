#pragma once

#include "../Params/Identifiers.h"
#include <array>

namespace GGrid
{
    // A directed edge from one graph node's output to another's input. Graph nodes are the
    // kMaxSlots regular module slots (0..kMaxSlots-1) plus two fixed pseudo-nodes: kInputNodeId
    // (the plugin's raw dry input -- output ports only, never a destination) and kOutputNodeId
    // (the final mix -- input ports only, never a source). See GGridAudioProcessor::connections
    // for the model these anchor: Input is the sole root, Output the sole sink, and a regular
    // module with no path from Input simply doesn't run at all (Bitwig-Grid-style explicit
    // patching, not an implicit per-node dry-through).
    struct Connection { int from = -1; int to = -1; };

    constexpr int kInputNodeId = kMaxSlots;
    constexpr int kOutputNodeId = kMaxSlots + 1;
    constexpr int kNumGraphNodes = kMaxSlots + 2;

    // Each graph node allows up to this many outgoing and this many incoming audio edges --
    // "4 outs / 4 ins" (matching 4 output nubs / 4 input dots each NodeComponent draws), enough
    // for real parallel-processing patches (e.g. one node splitting into low/mid/hi bands) without
    // a general N-port model.
    constexpr int kMaxPortsPerSide = 4;
    static constexpr int kMaxConnections = kNumGraphNodes * kMaxPortsPerSide;

    // Pure topology helper -- no audio/JUCE dependency beyond kMaxSlots, deliberately kept
    // separate from GGridAudioProcessor's buffer-summing loop so the graph algorithm itself
    // (topological order, roots, sinks, the cycle fallback) can be exercised directly by the
    // offline test harness without needing to construct a full AudioProcessor.
    struct ConnectionGraphResult
    {
        std::array<int, kNumGraphNodes> order {};   // topological processing order; first orderCount entries valid
        int orderCount = 0;
        std::array<bool, kNumGraphNodes> isRoot {}; // only ever true for kInputNodeId -- seeded with raw input
        std::array<bool, kNumGraphNodes> isSink {}; // only ever true for kOutputNodeId -- summed into the final mix
        std::array<int, kNumGraphNodes> outDegree {};
        std::array<int, kNumGraphNodes> inDegree {};
    };

    // Kahn's algorithm anchored at the Input pseudo-node as the sole root and Output as the sole
    // sink -- a regular module with zero incoming edges is NOT an implicit root under this model;
    // if it's genuinely unreachable from Input it never gets seeded with real signal and is
    // excluded from the order entirely (an unpatched module produces nothing, matching Bitwig
    // Grid). Fixed-size, no heap allocation, safe to call from the audio thread. Edges where
    // either endpoint isn't active are ignored entirely (as if they didn't exist).
    //
    // Reachability from Input is computed as its own separate BFS (below), independent of the
    // in-degree bookkeeping the topological pass uses -- this is what lets the defensive cycle
    // fallback at the end tell apart "stuck behind a cycle downstream of Input" (include it) from
    // "genuinely never patched into the signal path at all" (correctly exclude it), which a
    // single in-degree-counting pass can't distinguish on its own. Cycles are rejected at
    // connect-time (see GGridAudioProcessor::wouldCreateCycle) so one shouldn't be able to exist
    // in practice, but the fallback keeps this function correct (no hang, no dropped reachable
    // node) even if a malformed connections array somehow contains one anyway.
    inline ConnectionGraphResult buildProcessingOrder (
        const std::array<Connection, kMaxConnections>& connections, int numConnections,
        const std::array<bool, kNumGraphNodes>& active)
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

        std::array<bool, kNumGraphNodes> reachable {};
        std::array<int, kNumGraphNodes> reachQueue {};
        int reachCount = 0;
        if (active[(size_t) kInputNodeId])
        {
            reachable[(size_t) kInputNodeId] = true;
            reachQueue[(size_t) reachCount++] = kInputNodeId;
        }
        for (int qi = 0; qi < reachCount; ++qi)
        {
            const int i = reachQueue[(size_t) qi];
            for (int c = 0; c < numConnections; ++c)
            {
                if (connections[(size_t) c].from != i) continue;
                const int to = connections[(size_t) c].to;
                if (! active[(size_t) to] || reachable[(size_t) to]) continue;
                reachable[(size_t) to] = true;
                reachQueue[(size_t) reachCount++] = to;
            }
        }

        std::array<int, kNumGraphNodes> remainingIn = result.inDegree;

        if (active[(size_t) kInputNodeId])
            result.order[(size_t) result.orderCount++] = kInputNodeId;

        std::array<bool, kNumGraphNodes> processed {};
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

        for (int i = 0; i < kNumGraphNodes; ++i)
            if (active[(size_t) i] && reachable[(size_t) i] && ! processed[(size_t) i])
                result.order[(size_t) result.orderCount++] = i;

        result.isRoot[(size_t) kInputNodeId] = active[(size_t) kInputNodeId];
        result.isSink[(size_t) kOutputNodeId] = active[(size_t) kOutputNodeId];

        return result;
    }
}

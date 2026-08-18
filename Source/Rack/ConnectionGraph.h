#pragma once

#include "../Params/Identifiers.h"
#include <array>

namespace GGrid
{
    // A directed edge from one slot's output to another slot's input. Two module types have
    // asymmetric ports: an Input-type slot has no input ports at all (it's a source of the
    // plugin's raw dry signal), an Output-type slot has no output ports at all (its summed input
    // contributes to the final mix) -- see GGridAudioProcessor::canAddConnection. Any number of
    // slots can be Input or Output type at once (every Input slot provides the same raw dry
    // signal; every Output slot's sum is added into the final mix), so the canvas can host
    // several independent racks/parallel signal paths side by side rather than a single fixed
    // entry/exit point.
    // fromPort: -1 means "unpinned" -- the vast majority of connections (every single-output-bus
    // module) leave this at -1, and NodeGraphEditor assigns a purely cosmetic ordinal dot for
    // drawing/hit-testing (see outputPortIndexForConnection), same behaviour as before this field
    // existed. A non-negative value pins the connection to a SPECIFIC output bus of a multi-bus
    // module (see RackModule::getNumOutputBuses/getOutputBusBuffer) -- e.g. Multipass's Low/Mid/
    // High bands -- both for which bus's buffer GGridAudioProcessor::processBlock actually reads
    // from, and for which physical dot the cable is drawn/grabbed at (so a "Low" cable stays
    // visually and functionally the Low band even as other cables come and go).
    struct Connection { int from = -1; int to = -1; int fromPort = -1; };

    // Each slot allows up to this many outgoing and this many incoming audio edges -- "4 outs /
    // 4 ins" (matching 4 output nubs / 4 input dots each NodeComponent draws), enough for real
    // parallel-processing patches (e.g. one node splitting into low/mid/hi bands) without a
    // general N-port model.
    constexpr int kMaxPortsPerSide = 4;
    static constexpr int kMaxConnections = kMaxSlots * kMaxPortsPerSide;

    // Pure topology helper -- no audio/JUCE dependency beyond kMaxSlots, deliberately kept
    // separate from GGridAudioProcessor's buffer-summing loop so the graph algorithm itself
    // (topological order, roots, sinks, the cycle fallback) can be exercised directly by the
    // offline test harness without needing to construct a full AudioProcessor.
    struct ConnectionGraphResult
    {
        std::array<int, kMaxSlots> order {};   // topological processing order; first orderCount entries valid
        int orderCount = 0;
        std::array<bool, kMaxSlots> isRoot {}; // true for every active Input-type slot
        std::array<bool, kMaxSlots> isSink {}; // true for every active Output-type slot
        std::array<int, kMaxSlots> outDegree {};
        std::array<int, kMaxSlots> inDegree {};
    };

    // Kahn's algorithm over a graph with potentially several roots (every active Input-type slot,
    // marked via isRootRole) and several sinks (every active Output-type slot, isSinkRole) -- see
    // GGridAudioProcessor::processBlock, which derives these role arrays from each slot's module
    // type. A regular module with no path from any root is excluded from the order entirely (an
    // unpatched module produces nothing, matching Bitwig Grid). Fixed-size, no heap allocation,
    // safe to call from the audio thread.
    //
    // Reachability from any root is computed as its own separate BFS (seeded from every root at
    // once), independent of the in-degree bookkeeping the topological pass uses -- this is what
    // lets the defensive cycle fallback at the end tell apart "stuck behind a cycle downstream of
    // a root" (include it) from "genuinely never patched into the signal path at all" (correctly
    // exclude it). Cycles are rejected at connect-time (see
    // GGridAudioProcessor::wouldCreateCycle), so one shouldn't exist in practice, but the
    // fallback keeps this function correct (no hang, no dropped reachable node) regardless.
    inline ConnectionGraphResult buildProcessingOrder (
        const std::array<Connection, kMaxConnections>& connections, int numConnections,
        const std::array<bool, kMaxSlots>& active,
        const std::array<bool, kMaxSlots>& isRootRole,
        const std::array<bool, kMaxSlots>& isSinkRole)
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

        std::array<bool, kMaxSlots> reachable {};
        std::array<int, kMaxSlots> reachQueue {};
        int reachCount = 0;
        for (int i = 0; i < kMaxSlots; ++i)
        {
            if (active[(size_t) i] && isRootRole[(size_t) i])
            {
                reachable[(size_t) i] = true;
                reachQueue[(size_t) reachCount++] = i;
            }
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

        std::array<int, kMaxSlots> remainingIn = result.inDegree;

        for (int i = 0; i < kMaxSlots; ++i)
            if (active[(size_t) i] && isRootRole[(size_t) i])
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
            if (active[(size_t) i] && reachable[(size_t) i] && ! processed[(size_t) i])
                result.order[(size_t) result.orderCount++] = i;

        for (int i = 0; i < kMaxSlots; ++i)
        {
            result.isRoot[(size_t) i] = active[(size_t) i] && isRootRole[(size_t) i];
            result.isSink[(size_t) i] = active[(size_t) i] && isSinkRole[(size_t) i];
        }

        return result;
    }
}

#ifndef LIVETRACE_RANDOM_MESH_TOPOLOGY_H
#define LIVETRACE_RANDOM_MESH_TOPOLOGY_H

#include "ns3/node-container.h"

#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace ns3
{
namespace livetrace
{

/**
 * Builds a random relay mesh network: Erdős–Rényi G(n,p), fixed as the sole
 * topology-generation model for the whole project (see docs/summaries/phase0
 * for the rationale -- it lets edge density be controlled independently of
 * node count, which is what makes N a clean first-class scale axis).
 *
 * No bottleneck through which all traffic must pass: connectivity is
 * mandatory, and generation is retried (or falls back to the giant
 * component) when a single articulation point would strand most of the
 * network behind it.
 */
class RandomMeshTopology
{
  public:
    struct BuildResult
    {
        NodeContainer nodes;
        uint32_t victimNodeId;
        std::vector<std::pair<uint32_t, uint32_t>> edges;
        uint32_t regenAttempts;
        uint32_t articulationPointCount;
        uint32_t nodesDroppedForConnectivity;
        uint32_t diameter;       // longest shortest-path (in hops) between any two nodes
        double avgPathLength;    // mean shortest-path length over all connected pairs
        std::string note;
    };

    RandomMeshTopology(uint32_t n, double p, uint32_t maxRegenAttempts, uint32_t seed);

    BuildResult Build();

  private:
    std::vector<std::pair<uint32_t, uint32_t>> GenerateEdges(uint32_t n);
    std::vector<uint32_t> LargestConnectedComponent(uint32_t n,
                                                      const std::vector<std::pair<uint32_t, uint32_t>>& edges);
    /// Finds articulation points and, for each, the size of the second-largest
    /// component left after removing it -- a large value means that vertex is
    /// a true bottleneck splitting the mesh into two substantial halves, not
    /// just a pendant leaf.
    void AnalyzeArticulation(uint32_t n,
                              const std::vector<std::pair<uint32_t, uint32_t>>& edges,
                              uint32_t& artCountOut,
                              uint32_t& worstSecondComponentOut);
    /// BFS from every node to get all-pairs shortest-path hop counts; returns
    /// the diameter (max) and mean over all connected ordered pairs. This is
    /// public topology information (like a network map), not attack ground
    /// truth -- used to size the attacker's stepping-stone chain length so
    /// that difficulty actually grows with N (see AttackerCampaign).
    void ComputePathStats(uint32_t n,
                           const std::vector<std::pair<uint32_t, uint32_t>>& edges,
                           uint32_t& diameterOut,
                           double& avgPathLengthOut);

    uint32_t m_n;
    double m_p;
    uint32_t m_maxRegenAttempts;
    std::mt19937 m_rng;
};

} // namespace livetrace
} // namespace ns3

#endif // LIVETRACE_RANDOM_MESH_TOPOLOGY_H

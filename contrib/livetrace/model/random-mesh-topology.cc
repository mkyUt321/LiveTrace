#include "random-mesh-topology.h"

#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/nix-vector-helper.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/string.h"

#include <algorithm>
#include <map>
#include <queue>
#include <sstream>

namespace ns3
{
namespace livetrace
{

namespace
{

// Bottleneck acceptance threshold: an articulation point whose removal
// leaves a second component larger than this fraction of the network is
// rejected as a genuine cross-mesh chokepoint. Pendant leaves (small second
// components) are normal for a sparse random graph and are accepted.
constexpr double kBottleneckFraction = 0.15;

Ipv4Address
SubnetBaseForEdge(uint32_t edgeIndex)
{
    uint32_t offset = edgeIndex * 4;
    uint32_t addr = 0x0A000000u + offset; // 10.0.0.0/8 space, /30 per edge
    return Ipv4Address(addr);
}

} // namespace

RandomMeshTopology::RandomMeshTopology(uint32_t n, double p, uint32_t maxRegenAttempts, uint32_t seed)
    : m_n(n),
      m_p(p),
      m_maxRegenAttempts(maxRegenAttempts),
      m_rng(seed)
{
}

std::vector<std::pair<uint32_t, uint32_t>>
RandomMeshTopology::GenerateEdges(uint32_t n)
{
    std::vector<std::pair<uint32_t, uint32_t>> edges;
    std::uniform_real_distribution<double> unif(0.0, 1.0);
    for (uint32_t i = 0; i < n; ++i)
    {
        for (uint32_t j = i + 1; j < n; ++j)
        {
            if (unif(m_rng) < m_p)
            {
                edges.emplace_back(i, j);
            }
        }
    }
    return edges;
}

std::vector<uint32_t>
RandomMeshTopology::LargestConnectedComponent(uint32_t n,
                                               const std::vector<std::pair<uint32_t, uint32_t>>& edges)
{
    std::map<uint32_t, std::vector<uint32_t>> adj;
    for (const auto& e : edges)
    {
        adj[e.first].push_back(e.second);
        adj[e.second].push_back(e.first);
    }

    std::vector<bool> visited(n, false);
    std::vector<uint32_t> best;
    for (uint32_t s = 0; s < n; ++s)
    {
        if (visited[s])
        {
            continue;
        }
        std::vector<uint32_t> comp;
        std::queue<uint32_t> q;
        q.push(s);
        visited[s] = true;
        while (!q.empty())
        {
            uint32_t u = q.front();
            q.pop();
            comp.push_back(u);
            for (uint32_t v : adj[u])
            {
                if (!visited[v])
                {
                    visited[v] = true;
                    q.push(v);
                }
            }
        }
        if (comp.size() > best.size())
        {
            best = comp;
        }
    }
    return best;
}

namespace
{

// Standard Tarjan articulation-point DFS.
void
ArticulationDfs(uint32_t u,
                 int parent,
                 const std::map<uint32_t, std::vector<uint32_t>>& adj,
                 std::vector<int>& disc,
                 std::vector<int>& low,
                 int& timer,
                 std::vector<bool>& isArt)
{
    disc[u] = low[u] = timer++;
    int children = 0;
    auto it = adj.find(u);
    if (it == adj.end())
    {
        return;
    }
    for (uint32_t v : it->second)
    {
        if (disc[v] == -1)
        {
            children++;
            ArticulationDfs(v, static_cast<int>(u), adj, disc, low, timer, isArt);
            low[u] = std::min(low[u], low[v]);
            if (parent != -1 && low[v] >= disc[u])
            {
                isArt[u] = true;
            }
            if (parent == -1 && children > 1)
            {
                isArt[u] = true;
            }
        }
        else if (static_cast<int>(v) != parent)
        {
            low[u] = std::min(low[u], disc[v]);
        }
    }
}

} // namespace

void
RandomMeshTopology::AnalyzeArticulation(uint32_t n,
                                         const std::vector<std::pair<uint32_t, uint32_t>>& edges,
                                         uint32_t& artCountOut,
                                         uint32_t& worstSecondComponentOut)
{
    std::map<uint32_t, std::vector<uint32_t>> adj;
    for (const auto& e : edges)
    {
        adj[e.first].push_back(e.second);
        adj[e.second].push_back(e.first);
    }

    std::vector<int> disc(n, -1), low(n, -1);
    std::vector<bool> isArt(n, false);
    int timer = 0;
    for (uint32_t u = 0; u < n; ++u)
    {
        if (disc[u] == -1)
        {
            ArticulationDfs(u, -1, adj, disc, low, timer, isArt);
        }
    }

    artCountOut = 0;
    worstSecondComponentOut = 0;
    for (uint32_t v = 0; v < n; ++v)
    {
        if (!isArt[v])
        {
            continue;
        }
        artCountOut++;

        // Component sizes of G - v.
        std::vector<bool> visited(n, false);
        visited[v] = true;
        std::vector<uint32_t> sizes;
        for (uint32_t s = 0; s < n; ++s)
        {
            if (visited[s])
            {
                continue;
            }
            uint32_t count = 0;
            std::queue<uint32_t> q;
            q.push(s);
            visited[s] = true;
            while (!q.empty())
            {
                uint32_t u = q.front();
                q.pop();
                count++;
                auto it = adj.find(u);
                if (it != adj.end())
                {
                    for (uint32_t w : it->second)
                    {
                        if (w != v && !visited[w])
                        {
                            visited[w] = true;
                            q.push(w);
                        }
                    }
                }
            }
            sizes.push_back(count);
        }
        std::sort(sizes.rbegin(), sizes.rend());
        uint32_t second = sizes.size() > 1 ? sizes[1] : 0;
        worstSecondComponentOut = std::max(worstSecondComponentOut, second);
    }
}

RandomMeshTopology::BuildResult
RandomMeshTopology::Build()
{
    BuildResult result;
    uint32_t n = m_n;
    std::vector<std::pair<uint32_t, uint32_t>> edges;
    uint32_t attempts = 0;
    uint32_t artCount = 0;
    uint32_t worstSecond = 0;
    uint32_t droppedNodes = 0;

    for (attempts = 1; attempts <= m_maxRegenAttempts; ++attempts)
    {
        edges = GenerateEdges(n);
        auto giant = LargestConnectedComponent(n, edges);
        if (giant.size() < n)
        {
            continue; // disconnected: retry with a fresh draw
        }
        AnalyzeArticulation(n, edges, artCount, worstSecond);
        if (worstSecond <= static_cast<uint32_t>(kBottleneckFraction * n))
        {
            break; // connected and free of a large-scale bottleneck
        }
    }

    if (attempts > m_maxRegenAttempts)
    {
        // Fall back: take the giant component of the last draw (guaranteed
        // connected check above may still have failed only on the bottleneck
        // criterion at this point) and accept it with a note.
        auto giant = LargestConnectedComponent(n, edges);
        droppedNodes = n - static_cast<uint32_t>(giant.size());
        if (droppedNodes > 0)
        {
            std::map<uint32_t, uint32_t> remap;
            for (size_t i = 0; i < giant.size(); ++i)
            {
                remap[giant[i]] = static_cast<uint32_t>(i);
            }
            std::vector<std::pair<uint32_t, uint32_t>> remapped;
            for (const auto& e : edges)
            {
                auto itA = remap.find(e.first);
                auto itB = remap.find(e.second);
                if (itA != remap.end() && itB != remap.end())
                {
                    remapped.emplace_back(itA->second, itB->second);
                }
            }
            edges = remapped;
            n = static_cast<uint32_t>(giant.size());
        }
    }

    result.nodes.Create(n);
    result.edges = edges;
    result.regenAttempts = attempts <= m_maxRegenAttempts ? attempts : m_maxRegenAttempts;
    result.articulationPointCount = artCount;
    result.nodesDroppedForConnectivity = droppedNodes;
    result.victimNodeId = 0;

    // Nix-vector routing computes routes on demand (and caches them) instead
    // of precomputing an all-pairs table up front like
    // Ipv4GlobalRoutingHelper -- which is well known not to scale past a few
    // hundred nodes. Empirically, global routing took >5 minutes of setup
    // alone at N=160 in this project's dense mesh; nix-vector is what makes
    // the N=320 cell of the scale sweep tractable at all.
    Ipv4NixVectorHelper nixRouting;
    InternetStackHelper stack;
    stack.SetRoutingHelper(nixRouting);
    stack.Install(result.nodes);

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("2ms"));

    Ipv4AddressHelper address;
    for (uint32_t i = 0; i < edges.size(); ++i)
    {
        NetDeviceContainer devs = p2p.Install(result.nodes.Get(edges[i].first), result.nodes.Get(edges[i].second));
        address.SetBase(SubnetBaseForEdge(i), Ipv4Mask("255.255.255.252"));
        address.Assign(devs);
    }

    std::ostringstream note;
    note << "topology=erdos_renyi n=" << n << " p=" << m_p << " edges=" << edges.size()
         << " regen_attempts=" << result.regenAttempts << " articulation_points=" << artCount
         << " worst_second_component=" << worstSecond << " nodes_dropped=" << droppedNodes;
    result.note = note.str();

    return result;
}

} // namespace livetrace
} // namespace ns3

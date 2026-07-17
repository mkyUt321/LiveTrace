#include "ns3/livetrace-config.h"
#include "ns3/observation-log.h"
#include "ns3/random-mesh-topology.h"
#include "ns3/test.h"

#include <cstdio>
#include <fstream>
#include <map>
#include <queue>

using namespace ns3;
using namespace ns3::livetrace;

namespace
{

bool
IsConnected(const RandomMeshTopology::BuildResult& r)
{
    uint32_t n = r.nodes.GetN();
    if (n == 0)
    {
        return false;
    }
    std::map<uint32_t, std::vector<uint32_t>> adj;
    for (const auto& e : r.edges)
    {
        adj[e.first].push_back(e.second);
        adj[e.second].push_back(e.first);
    }
    std::vector<bool> visited(n, false);
    std::queue<uint32_t> q;
    q.push(0);
    visited[0] = true;
    uint32_t count = 0;
    while (!q.empty())
    {
        uint32_t u = q.front();
        q.pop();
        count++;
        for (uint32_t v : adj[u])
        {
            if (!visited[v])
            {
                visited[v] = true;
                q.push(v);
            }
        }
    }
    return count == n;
}

} // namespace

class TopologyConnectivityTestCase : public TestCase
{
  public:
    TopologyConnectivityTestCase()
        : TestCase("RandomMeshTopology produces a connected, bottleneck-free mesh")
    {
    }

  private:
    void DoRun() override
    {
        RandomMeshTopology topo(30, 0.15, 50, 42);
        RandomMeshTopology::BuildResult result = topo.Build();
        NS_TEST_ASSERT_MSG_GT(result.nodes.GetN(), 0u, "topology should produce at least one node");
        NS_TEST_ASSERT_MSG_GT(result.edges.size(), 0u, "topology should produce at least one edge");
        NS_TEST_ASSERT_MSG_EQ(IsConnected(result), true, "topology must be connected");
    }
};

class ConfigLoaderTestCase : public TestCase
{
  public:
    ConfigLoaderTestCase()
        : TestCase("LiveTraceConfig parses flat and one-level-nested YAML scalars")
    {
    }

  private:
    void DoRun() override
    {
        std::string path = "livetrace-test-config.yaml";
        {
            std::ofstream out(path);
            out << "seed: 7\n";
            out << "topology:\n";
            out << "  n: 64\n";
            out << "  p: 0.2\n";
            out << "sweep:\n";
            out << "  n_values: [10, 20, 30]\n";
        }
        LiveTraceConfig cfg;
        bool ok = cfg.Load(path);
        NS_TEST_ASSERT_MSG_EQ(ok, true, "config file should load");
        NS_TEST_ASSERT_MSG_EQ(cfg.GetInt("seed", -1), 7, "flat scalar seed");
        NS_TEST_ASSERT_MSG_EQ(cfg.GetInt("topology.n", -1), 64, "nested scalar topology.n");
        NS_TEST_ASSERT_MSG_EQ_TOL(cfg.GetDouble("topology.p", -1.0), 0.2, 1e-9, "nested scalar topology.p");
        auto list = cfg.GetIntList("sweep.n_values");
        NS_TEST_ASSERT_MSG_EQ(list.size(), 3u, "n_values list length");
        NS_TEST_ASSERT_MSG_EQ(list[1], 20, "n_values second element");
        std::remove(path.c_str());
    }
};

class ObservationLogTestCase : public TestCase
{
  public:
    ObservationLogTestCase()
        : TestCase("ObservationLog windowed queries by node and by flow")
    {
    }

  private:
    void DoRun() override
    {
        ObservationLog log;
        log.RecordSend(0, "flowA", 1.0, 100);
        log.RecordRecv(1, "flowA", 1.02, 100);
        log.RecordSend(1, "flowB", 1.05, 100);
        log.RecordRecv(2, "flowB", 1.07, 100);

        auto atNode1 = log.EventsAtNode(1, 0.0, 5.0);
        NS_TEST_ASSERT_MSG_EQ(atNode1.size(), 2u, "node 1 should see 2 events (recv flowA, send flowB)");

        auto flowAEvents = log.EventsForFlow("flowA", 0.0, 5.0);
        NS_TEST_ASSERT_MSG_EQ(flowAEvents.size(), 2u, "flowA should have send+recv events");

        auto windowed = log.EventsForFlow("flowA", 0.0, 1.01);
        NS_TEST_ASSERT_MSG_EQ(windowed.size(), 1u, "time window should exclude the later recv");
    }
};

class LiveTraceTestSuite : public TestSuite
{
  public:
    LiveTraceTestSuite()
        : TestSuite("livetrace", Type::UNIT)
    {
        AddTestCase(new TopologyConnectivityTestCase(), TestCase::Duration::QUICK);
        AddTestCase(new ConfigLoaderTestCase(), TestCase::Duration::QUICK);
        AddTestCase(new ObservationLogTestCase(), TestCase::Duration::QUICK);
    }
};

static LiveTraceTestSuite g_liveTraceTestSuite;

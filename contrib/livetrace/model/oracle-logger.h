#ifndef LIVETRACE_ORACLE_LOGGER_H
#define LIVETRACE_ORACLE_LOGGER_H

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace ns3
{
namespace livetrace
{

/**
 * Ground-truth logger. Writes JSONL records of the true actor identity and
 * the true stepping-stone chain for every attack burst.
 *
 * INVARIANT: this class is instantiated only by the simulation driver and
 * handed to the attacker application. It must never be passed to
 * ObservationLog, TimingCorrelator, TracebackObserver, or the
 * re-identification engine -- those components must solve the problem from
 * observable traffic alone. Evaluation scripts read this log only after the
 * run completes, to score the system's output against ground truth.
 */
class OracleLogger
{
  public:
    explicit OracleLogger(const std::string& path);
    ~OracleLogger();

    /// Record one burst's ground truth: which actor it belongs to and the
    /// true overlay chain of node ids, origin first, victim last.
    void LogBurst(uint32_t burstId,
                  uint32_t trueActorId,
                  double startTimeS,
                  const std::vector<uint32_t>& trueChainNodeIds);

    /// Record why the topology generator made the choices it made (model,
    /// seed, regeneration attempts) so the run is auditable.
    void LogTopologyNote(const std::string& note);

  private:
    std::ofstream m_out;
};

} // namespace livetrace
} // namespace ns3

#endif // LIVETRACE_ORACLE_LOGGER_H

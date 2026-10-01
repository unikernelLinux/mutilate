/* -*- c++ -*- */
#ifndef AGENTSTATS_H
#define AGENTSTATS_H

class AgentStats {
public:
  uint64_t rx_bytes, tx_bytes;
  uint64_t gets, sets, get_misses;
  uint64_t skips;

  // Not start/stop: agent clocks aren't synced with the master's.
  double duration;

  double client_lag_sum, client_lag_max;
  double depth_lag_sum, depth_lag_max;
  uint64_t depth_blocked;
};

#endif // AGENTSTATS_H

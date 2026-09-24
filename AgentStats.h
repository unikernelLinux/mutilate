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
};

#endif // AGENTSTATS_H

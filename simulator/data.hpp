#pragma once

#define LOGGING 1

#include "spdlog/spdlog.h"
#include <boost/date_time/posix_time/posix_time.hpp>
#include <random>
#include "simcpp20/simcpp20.hpp"

namespace pt = boost::posix_time;

typedef long long Time;

extern bool colored;

struct config
{
  pt::ptime start_time, end_time;
  std::exponential_distribution<> dispatcher_call_dist_red, dispatcher_call_dist_yellow, dispatcher_call_dist_green, dispatcher_call_dist_white;
  std::uniform_int_distribution<> max_wait_time_dist;
  std::exponential_distribution<> arrival_interval_dist;
  std::exponential_distribution<> treatment_duration_dist;
  std::default_random_engine gen;
  std::string emergencies_filename;
  std::string ambulances_filename;
  std::string hospitals_filename;
  std::string osrm_filename;
  bool preemptable;
};

class SimulationEntity
{
public:
  simcpp20::simulation<Time>& sim;
  void preempt() noexcept {
    preempt_.trigger();
    preempt_ = sim.event<Time>();
  }
  void abort() noexcept {
    abort_.trigger();
  }
protected:
  SimulationEntity(simcpp20::simulation<Time>& sim, config& conf) : sim(sim), conf(conf), preempt_(sim.event<Time>()), abort_(sim.event<Time>()) {}
  config& conf;
  simcpp20::event<Time> preempt_, abort_;
};

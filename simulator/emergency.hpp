#pragma once

#include "data.hpp"
#include "hospital.hpp"
#include "ambulance.hpp"
#include "simcpp20/simcpp20.hpp"
#include "routing.hpp"
#include <limits>

class Dispatcher;

class Emergency : public SimulationEntity
{
  friend class Dispatcher;
public:
  Emergency(simcpp20::simulation<Time>& sim, config& conf, Dispatcher& dispatcher) : SimulationEntity(sim, conf), current_state(UNSCHEDULED), dispatcher(dispatcher), treatment_duration(200 + conf.treatment_duration_dist(conf.gen)), start_serving_time(std::numeric_limits<Time>::max()), reaching_time(std::numeric_limits<Time>::max()), at_hospital_time(std::numeric_limits<Time>::max())
  {}
protected:
  simcpp20::event<Time> generate();  
public:
  enum Code
  {
    RED,
    YELLOW,
    GREEN,
    WHITE,
    BLACK
  };
  enum State 
  {
    UNSCHEDULED,
    SCHEDULED,
    WAITING_AMBULANCE,    
    AMBULANCE_ASSIGNED,
    ON_TREATMENT,
    TO_HOSPITAL,
    ENDED
  };
  size_t index;
  std::string id;
  std::string municipality;
  bool needs_hospital;
  Hospital::Type needed_hospital;
  std::string actual_hospital;
  Code triage;
  Coordinate place;
  pt::ptime timestamp;
  Time treatment_duration;
  Time occurring_time, start_serving_time, reaching_time, at_hospital_time;
  State current_state;
  std::shared_ptr<Hospital> assigned_hospital;
  Dispatcher& dispatcher;
  
  // TODO: create state management functions (i.e., on_treatment(), etc.)
  
  static void source(std::istream &is, simcpp20::simulation<Time> &sim, config &conf, Dispatcher& dispatcher);
protected:
  static std::vector<std::shared_ptr<Emergency>> emergencies;
};

std::ostream& operator<<(std::ostream &os, const Emergency::Code& c);

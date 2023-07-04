#pragma once

#include "helpers.hpp"

class Dispatcher : public SimulationEntity
{
  typedef simcpp20::value_event<std::shared_ptr<Ambulance>, Time> AmbulanceAssignment;
public:
  Dispatcher(simcpp20::simulation<Time>& sim, config& conf, Routing& routing) : SimulationEntity(sim, conf), routing(routing) { cleanup(); }
  simcpp20::event<Time> new_emergency(std::shared_ptr<Emergency> e);
  simcpp20::event<Time> preempted_emergency(std::shared_ptr<Emergency> e);
  simcpp20::event<Time> assignable_ambulance(std::shared_ptr<Ambulance> a);
  void ambulance_available(std::shared_ptr<Ambulance> a);
  void emergency_served(std::shared_ptr<Emergency> e);
  simcpp20::event<Time> ambulance_unavailable(std::shared_ptr<Ambulance> a);
protected:
  simcpp20::event<Time> cleanup();
  // The following two methods implement the dispatching policy
  std::vector<std::pair<std::shared_ptr<Ambulance>, Routing::Segment>> get_ambulances(std::shared_ptr<Emergency> e, Ambulance::Type t, units::length::kilometer_t d_threshold, units::time::minute_t t_threshold);
  std::map<Emergency::Code, std::list<std::shared_ptr<Emergency>>> waiting_emergencies, serving_emergencies;
  std::list<std::shared_ptr<Ambulance>> available_ambulances;
  Routing& routing;
};

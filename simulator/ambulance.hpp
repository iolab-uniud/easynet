#pragma once

#include "data.hpp"
#include "routing.hpp"

class Emergency;
class Dispatcher;

class Ambulance : public SimulationEntity {
  friend class Dispatcher;
public:
  Ambulance(simcpp20::simulation<Time>& sim, config& conf, Dispatcher& dispatcher, Routing& routing) : SimulationEntity(sim, conf), current_emergency(nullptr), moving(false), current_state(UNAVAILABLE), dispatcher(dispatcher), rescue_finished_(sim.event<Time>()), routing(routing) {}
  enum Type
  {
    ALS,
    BLS,
    MV
  };
  enum State
  {
    UNAVAILABLE,
    WAITING_AT_BASE,
    ASSIGNED,
    TO_EMERGENCY,
    ON_TREATMENT,
    TO_HOSPITAL,
    CLEANING,
    TO_BASE,
    PREEMPTED
  };
  bool moving;
  size_t index;
  std::string id;
  std::string description;
  Type type;
  Coordinate base;
  Time start_duty, end_duty;
  Time shift_start, shift_end;
  // working variables for handling emergency
  // TODO: consider refactoring into a Rescue class
  std::shared_ptr<Emergency> current_emergency;
  State current_state;  
  void assign(std::shared_ptr<Emergency> e, Routing::Segment s);
  void assign_pair(std::shared_ptr<Emergency> e, Routing::Segment s, std::shared_ptr<Ambulance> mv, Routing::Segment mv_s);
  simcpp20::event<Time> rescue_finished();
protected:
  Time travel_start, travel_time;
  Routing::Segment current_segment;
  Coordinate current_position_;
  std::list<Routing::Segment> current_route;
  bool preemptable(std::shared_ptr<Emergency> e) const;
  inline bool waiting() const {
    return current_state == WAITING_AT_BASE;
  }
  inline bool assigned() const {
    return current_state == ASSIGNED || current_state == TO_EMERGENCY || current_state == ON_TREATMENT || current_state == TO_HOSPITAL || current_state == CLEANING;
  }
  simcpp20::event<Time> shift();
  simcpp20::event<Time> rescue_started(std::shared_ptr<Emergency> e, Routing::Segment initial_segment);
  simcpp20::event<Time> pair_rescue_started(std::shared_ptr<Emergency> e, Routing::Segment s, std::shared_ptr<Ambulance> mv, Routing::Segment mv_s);
  simcpp20::event<Time> to_emergency(bool pair=false);
  simcpp20::event<Time> treatment();
  simcpp20::event<Time> to_hospital();
  simcpp20::event<Time> cleaning();
  simcpp20::event<Time> to_base();
  simcpp20::event<Time> travel_to(const Routing::Segment& s);
  Coordinate current_position();
  simcpp20::event<Time> rescue_finished_;
public:
  static void source(std::istream &is, simcpp20::simulation<Time> &sim, config &conf, Dispatcher& dispatcher, Routing& routing);
protected:
  static std::vector<std::shared_ptr<Ambulance>> ambulances;
  Dispatcher& dispatcher;
  Routing& routing;
};

namespace std {
string to_string(Ambulance::State s);
}

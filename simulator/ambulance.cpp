#include "data.hpp"
#include "ambulance.hpp"
#include "routing.hpp"
#include "helpers.hpp"
#include "dispatcher.hpp"
#include <iostream>
#include "units.h"
#include "range/v3/view/filter.hpp"
#include "range/v3/view/zip.hpp"
#include "range/v3/view/transform.hpp"
#include "range/v3/range/conversion.hpp"
#include "range/v3/action/sort.hpp"
#include <concepts>

using namespace ranges;

std::istream &operator>>(std::istream &is, Ambulance::Type &type)
{
  std::string tmp;
  is >> tmp;
  if (tmp == "ALS")
    type = Ambulance::Type::ALS;
  else if (tmp == "BLS")
    type = Ambulance::Type::BLS;
  else if (tmp == "MV")
    type = Ambulance::Type::MV;
  else
    throw std::logic_error("Ambulance type (" + tmp + ") not recognized");
  return is;
}

std::ostream& operator<<(std::ostream &os, const Ambulance::Type& t) {
  switch (t) {
    case Ambulance::Type::ALS:
      os << "ALS";
      break;
    case Ambulance::Type::BLS:
      os << "BLS";
      break;
    case Ambulance::Type::MV:
      os << "MV";
      break;
  }
  return os;
}


std::istream &operator>>(std::istream &is, Ambulance &a)
{
  unsigned long hours, minutes;
  std::string tmp;
  is >> a.id >> a.description >> a.type >> a.base;
  std::getline(is, tmp, ':');
  hours = std::stoul(tmp);
  std::getline(is, tmp, ' ');
  minutes = std::stoul(tmp);
  a.shift_start = (hours * 60 + minutes) * 60; // TODO: time granularity is fixed to second
  std::getline(is, tmp, ':');
  hours = std::stoul(tmp);
  std::getline(is, tmp);
  minutes = std::stoul(tmp);
  a.shift_end = (hours * 60 + minutes) * 60; // TODO: time granularity is fixed to second
  return is;
}

simcpp20::event<Time> Ambulance::shift()
{
  std::shared_ptr<Ambulance> a = ambulances[index];
  Time offset = (conf.start_time - pt::ptime(conf.start_time.date())).total_seconds();
  // it assumes that sim.now() is 0 and synchronous with midnight
  assert(sim.now() == 0);
  // synchronize with the start of the simulation
  sim.timeout(offset);
  Time current_day = sim.now() / 86400, current_daystart = floor(current_day) * 86400, current_daytime = sim.now() % 86400 + offset;
  Time limit = (conf.end_time - conf.start_time).total_seconds();
  if (shift_start > shift_end) { // overnight shift ambulance
    start_duty = std::max(current_daystart - current_daytime - shift_start, Time{0});
    end_duty = current_daystart - current_daytime + shift_end;
  }
  else if (shift_start < shift_end) { // dayshift ambulance
    start_duty = current_daystart - current_daytime + shift_start;
    end_duty = current_daystart - current_daytime + shift_end;
  }
  if (shift_start + 86400 == shift_end) { // 24h ambulances, just start the service and wait for the end
#ifdef LOGGING
    spdlog::info("[{}] Ambulance {} starts 24h service", std::to_string(conf.start_time, sim.now()), *this);
#endif
    start_duty = current_daystart - current_daytime;
    end_duty = limit;
    current_state = WAITING_AT_BASE;
    SimulationData::log_ambulance(*this, sim.now(), conf.start_time);
    current_position_ = base;
    dispatcher.ambulance_available(ambulances[index]);
    co_return;
  }
  while (start_duty <= limit) {
    if (start_duty >= sim.now())
    {
#ifdef LOGGING
      spdlog::debug("[{}] Ambulance {} scheduled for service from {}", std::to_string(conf.start_time, sim.now()), *this, std::to_string(conf.start_time, start_duty));
#endif
      current_state = UNAVAILABLE;
      co_await sim.timeout(start_duty - sim.now());
    }
#ifdef LOGGING
    spdlog::info("[{}] Ambulance {} starts service up to {}", std::to_string(conf.start_time, sim.now()), *this, std::to_string(conf.start_time, end_duty));
#endif
    current_state = WAITING_AT_BASE;
    SimulationData::log_ambulance(*this, sim.now(), conf.start_time);
    current_position_ = base;
    dispatcher.ambulance_available(ambulances[index]);
    co_await sim.timeout(end_duty - sim.now());
    co_await dispatcher.ambulance_unavailable(ambulances[index]);
    current_state = UNAVAILABLE;
    SimulationData::log_ambulance(*this, sim.now(), conf.start_time);
#ifdef LOGGING
    spdlog::info("[{}] Ambulance {} ends service", std::to_string(conf.start_time, sim.now()), *this);
#endif
    if (start_duty == 0 && shift_start > 0) { // overnight shift after the first occurrence
      start_duty = current_daystart - current_daytime + shift_start;
    } else {
      start_duty += 86400;
    }
    end_duty += 86400;
  }
}

bool Ambulance::preemptable(std::shared_ptr<Emergency> e) const {
  if (!conf.preemptable)
    return false;
  if (current_state == TO_BASE)
    return true;
  if (current_state == TO_EMERGENCY)
    return (e->triage == Emergency::RED || e->triage == Emergency::YELLOW) && (current_emergency->triage == Emergency::GREEN || current_emergency->triage == Emergency::WHITE) && (travel_start + travel_time > sim.now());
  return false;
}

void Ambulance::assign(std::shared_ptr<Emergency> e, Routing::Segment initial_segment) {
  assert(type != MV);
  current_state = ASSIGNED;
#ifdef LOGGING
  spdlog::info("[{}] Emergency {} assigned to ambulance {}", std::to_string(conf.start_time, sim.now()), *e, *this);
#endif
  rescue_started(e, initial_segment);
}

simcpp20::event<Time> Ambulance::rescue_started(std::shared_ptr<Emergency> e, Routing::Segment initial_segment) {
  auto a = ambulances[index];
  co_await sim.timeout(0); // in order to maintain order of events (i.e., pre-empt first, then proceed with the new assignment)
  assert(current_emergency == nullptr);
  current_segment = initial_segment;
  current_emergency = e;
  current_emergency->start_serving_time = sim.now();
  current_emergency->current_state = Emergency::AMBULANCE_ASSIGNED;
  // emergency management
  co_await to_emergency();
}

void Ambulance::assign_pair(std::shared_ptr<Emergency> e, Routing::Segment initial_segment, std::shared_ptr<Ambulance> mv, Routing::Segment mv_initial_segment) {
  assert(type != MV && mv->type == MV);
  current_state = ASSIGNED;
  mv->current_state = ASSIGNED;
#ifdef LOGGING
  spdlog::info("[{}] Emergency {} assigned to ambulance {} and medical vehicle {}", std::to_string(conf.start_time, sim.now()), *e, *this, *mv);
#endif
  pair_rescue_started(e, initial_segment, mv, mv_initial_segment);
}

simcpp20::event<Time> Ambulance::pair_rescue_started(std::shared_ptr<Emergency> e, Routing::Segment initial_segment, std::shared_ptr<Ambulance> mv, Routing::Segment mv_initial_segment) {
  co_await sim.timeout(0); // in order to maintain order of events (i.e., pre-empt first, then proceed with the new assignment)
  assert(current_emergency == nullptr && mv->current_emergency == nullptr);
  current_segment = initial_segment; mv->current_segment = mv_initial_segment;
  current_emergency = e; mv->current_emergency = e;
  current_emergency->start_serving_time = sim.now();
  current_emergency->current_state = Emergency::AMBULANCE_ASSIGNED;
  // emergency management
  co_await sim.all_of(to_emergency(true), mv->to_emergency(true));
  co_await sim.all_of(treatment(), mv->treatment());
}
  
simcpp20::event<Time> Ambulance::to_emergency(bool pair) {
  current_state = TO_EMERGENCY;
  auto e = current_emergency;
  auto s = current_segment;
  SimulationData::log_ambulance(*this, *e, sim.now(), conf.start_time);
#ifdef LOGGING
  if (current_state == WAITING_AT_BASE) {
    spdlog::info("[{}] Ambulance {} going to emergency {} from base {} ({}, {})", std::to_string(conf.start_time, sim.now()), *this, *e, std::to_string(current_position_), units::time::to_string(s.duration), units::length::to_string(s.distance));
  } else {
    spdlog::info("[{}] Ambulance {} going to emergency {} from {} ({}, {})", std::to_string(conf.start_time, sim.now()), *this, *e, std::to_string(current_position_), units::time::to_string(s.duration), units::length::to_string(s.distance));
  }
#endif
  if (!pair) {
    // it's pre-emptable only if it is not a pair travel
    auto ev = travel_to(s);
    co_await sim.any_of(ev, preempt_);
    if (!ev.processed()) {
#ifdef LOGGING
      spdlog::info("[{}] Ambulance {} pre-empted discarding emergency {}", std::to_string(conf.start_time, sim.now()), *this, *e);
#endif
      e->start_serving_time = std::numeric_limits<Time>::max();
      // TODO: incorporate in a function
      rescue_finished_.trigger();
      rescue_finished_ = sim.event<Time>();
      current_state = PREEMPTED;
      SimulationData::log_ambulance(*this, *e, sim.now(), conf.start_time);
      e->current_state = Emergency::WAITING_AMBULANCE;
      current_emergency = nullptr;
      dispatcher.preempted_emergency(e);
      co_return;
    }
  } else {
    co_await travel_to(s);
  }
  e->reaching_time = std::min(e->reaching_time, sim.now());
#ifdef LOGGING
  spdlog::info("[{}] Ambulance {} reached emergency {} after {}", std::to_string(conf.start_time, sim.now()), *this, *e, units::time::to_string(units::time::minute_t(units::time::second_t(sim.now() - e->occurring_time))));
  if (e->triage != Emergency::GREEN && e->triage != Emergency::WHITE && e->reaching_time - e->occurring_time > SERVICE_TIME_THRESHOLD) {
    spdlog::warn("[{}] Emergency {} service time not met for {}", std::to_string(conf.start_time, sim.now()), *e, units::time::to_string(units::time::second_t(e->reaching_time - e->occurring_time - SERVICE_TIME_THRESHOLD)));
  }
#endif
#ifdef DEBUG
  assert(Routing::haversine(current_position_, e->place) < units::length::meter_t(100.0));
#endif
  if (!pair)
    co_await treatment();
}
  
simcpp20::event<Time> Ambulance::treatment() {
  current_state = ON_TREATMENT;
  auto e = current_emergency;
  auto a = ambulances[index];
  SimulationData::log_ambulance(*this, *e, sim.now(), conf.start_time);
#ifdef LOGGING
  spdlog::info("[{}] Ambulance {} treating emergency {} for {}", std::to_string(conf.start_time, sim.now()), *this, *e, units::time::to_string(units::time::second_t(e->treatment_duration)));
#endif
  // TODO: can be preempted?
  co_await sim.timeout(e->treatment_duration);
  if (e->needs_hospital)
    co_await to_hospital();
  else {
    SimulationData::log_rescue(*e, *this, conf.start_time);
    current_emergency = nullptr;
    co_await to_base();
  }
}
  
simcpp20::event<Time> Ambulance::to_hospital() {
  current_state = TO_HOSPITAL;
  auto e = current_emergency;
  SimulationData::log_ambulance(*this, *e, sim.now(), conf.start_time);
#ifdef LOGGING
  spdlog::info("[{}] Ambulance {} finished treating emergency {}", std::to_string(conf.start_time, sim.now()), *this, *e);
#endif

  // searching hospital
  auto compatible_hospitals = Hospital::hospitals | views::filter([e](auto h) { return (e->needed_hospital == Hospital::SPOKE && h->type != Hospital::PEDIATRIC) || h->type == e->needed_hospital; });
  std::vector<Routing::Segment> result = routing.compute_distances(e->place, compatible_hospitals | views::transform([](auto h) { return h->place; }) | to<std::list>());
  auto distances = views::zip(compatible_hospitals, result) | to<std::vector> | actions::sort([](auto p1, auto p2) { return p1.second.duration < p2.second.duration; });
  auto h = distances.front().first;
  auto s = distances.front().second;
#ifdef LOGGING
  spdlog::info("[{}] Ambulance {} going to hospital {} for emergency {} ({}, {})", std::to_string(conf.start_time, sim.now()), *this, *h, *e, units::time::to_string(s.duration), units::length::to_string(s.distance));
#endif
  e->assigned_hospital = h;
  co_await travel_to(s);
#ifdef LOGGING
  spdlog::info("[{}] Ambulance {} reached hospital {} for emergency {}", std::to_string(conf.start_time, sim.now()), *this, *h, *e);
#endif
  e->at_hospital_time = sim.now();
  if (type != MV) {
    const std::shared_ptr<Ambulance> a = ambulances[index];
    SimulationData::log_rescue(*e, *this, conf.start_time);
#ifdef LOGGING
    spdlog::info("[{}] Ambulance {} discharging emergency {} at hospital {}", std::to_string(conf.start_time, sim.now()), *this, *e, *h);
#endif
    co_await sim.timeout(DISCHARGING_TIME);
    dispatcher.emergency_served(e);
    current_emergency = nullptr;
    co_await cleaning();
  } else {
    // TODO: medical vehicle do not need cleaning
    current_emergency = nullptr;
    co_await to_base();
  }
}
  
simcpp20::event<Time> Ambulance::cleaning() {
  current_state = CLEANING;
  SimulationData::log_ambulance(*this, sim.now(), conf.start_time);
#ifdef LOGGING
  spdlog::info("[{}] Ambulance {} cleaning", std::to_string(conf.start_time, sim.now()), *this);
#endif
  co_await sim.timeout(CLEANING_TIME);
  co_await to_base();
}

simcpp20::event<Time> Ambulance::to_base() {
  // going to base
  auto s = routing.compute_distances(current_position(), base);
  Time end_travel = sim.now() + Time(s.duration / units::time::second_t(1.0));
  if (end_travel < end_duty && s.distance < DISTANCE_THRESHOLD) {
    current_state = TO_BASE;
    SimulationData::log_ambulance(*this, sim.now(), conf.start_time);
#ifdef LOGGING
  spdlog::info("[{}] Ambulance {} going to base ({}, {})", std::to_string(conf.start_time, sim.now()), *this, units::time::to_string(s.duration), units::length::to_string(s.distance));
#endif
    dispatcher.assignable_ambulance(ambulances[index]);
  } else {
#ifdef LOGGING
    if (end_travel > end_duty)
      spdlog::info("[{}] Ambulance {} ends shift at {}, going to base ({}, {})", std::to_string(conf.start_time, sim.now()), *this, std::to_string(conf.start_time, end_duty), units::time::to_string(s.duration), units::length::to_string(s.distance));
    else
      spdlog::info("[{}] Ambulance {} going to base (not preemtable {}, {})", std::to_string(conf.start_time, sim.now()), *this, units::time::to_string(s.duration), units::length::to_string(s.distance));
#endif
    current_state = UNAVAILABLE;
    SimulationData::log_ambulance(*this, sim.now(), conf.start_time);
  }
  auto ev = travel_to(s);
  co_await sim.any_of(ev, preempt_);
  if (ev.processed()) {
    if (end_duty > sim.now()) {
#ifdef LOGGING
      spdlog::info("[{}] Ambulance {} back to base and waiting", std::to_string(conf.start_time, sim.now()), *this);
#endif
      current_state = WAITING_AT_BASE;
      dispatcher.assignable_ambulance(ambulances[index]);
      SimulationData::log_ambulance(*this, sim.now(), conf.start_time);
    } else {
#ifdef LOGGING
    spdlog::info("[{}] Ambulance {} back to base, finished shift at {}", std::to_string(conf.start_time, sim.now()), *this, std::to_string(conf.start_time, end_duty));
#endif
      current_state = UNAVAILABLE;
      SimulationData::log_ambulance(*this, sim.now(), conf.start_time);
    }
  } else {
    current_state = PREEMPTED;
    SimulationData::log_ambulance(*this, sim.now(), conf.start_time);
#ifdef LOGGING
    spdlog::info("[{}] Ambulance {} pre-empted while going back to base", std::to_string(conf.start_time, sim.now()), *this);
#endif
  }
  // TODO: incorporate in a function
  rescue_finished_.trigger();
  rescue_finished_ = sim.event<Time>();
}

simcpp20::event<Time> Ambulance::travel_to(const Routing::Segment& s) {
  current_segment = s;
  current_route.clear();
  moving = true;
  travel_start = sim.now();
  travel_time = s.duration / units::time::second_t(1.0);
  auto ev = sim.timeout(travel_time);
  co_await sim.any_of(ev, preempt_);
  if (!ev.processed()) {
    current_position_ = current_position();
  }
  else {
    current_position_ = s.end_point;
    moving = false;
  }
}

Coordinate Ambulance::current_position() {
  // FIXME: if currently on the highway it should wait to 
  if (!moving)
    return current_position_;
  if (current_route.size() == 0) {
    current_route = routing.compute_route(current_segment.start_point, current_segment.end_point);
  }
  auto accumulated_time = units::time::second_t(travel_start), now = units::time::second_t(sim.now()), finish_time = units::time::second_t(travel_start + travel_time);
  if (finish_time > now) {
    Coordinate position = current_segment.start_point;
    for (auto leg : current_route) {
      position = leg.start_point;
      if (accumulated_time > now) {
        return position;
      }
      accumulated_time += leg.duration;
    }
  }
  current_position_ = current_segment.end_point;
  return current_position_;
}

void Ambulance::source(std::istream &is, simcpp20::simulation<Time> &sim, config &conf, Dispatcher& dispatcher, Routing& routing)
{
  while (!is.eof())
  {
    auto a = std::make_shared<Ambulance>(sim, conf, dispatcher, routing);
    is >> *a;
    ambulances.push_back(a);
    a->index = ambulances.size() - 1;
    a->shift();
  }
#ifdef LOGGING
  spdlog::debug("Read {} ambulances", ambulances.size());
#endif
}

simcpp20::event<Time> Ambulance::rescue_finished() {
  return rescue_finished_;
}

std::vector<std::shared_ptr<Ambulance>> Ambulance::ambulances;

std::string std::to_string(Ambulance::State s) {
  switch (s) {
    case Ambulance::UNAVAILABLE: return "UNAVAILABLE";
    case Ambulance::WAITING_AT_BASE: return "WAITING_AT_BASE";
    case Ambulance::ASSIGNED: return "ASSIGNED";
    case Ambulance::TO_EMERGENCY: return "TO_EMERGENCY";
    case Ambulance::ON_TREATMENT: return "ON_TREATMENT";
    case Ambulance::TO_HOSPITAL: return "TO_HOSPITAL";
    case Ambulance::CLEANING: return "CLEANING";
    case Ambulance::TO_BASE: return "TO_BASE";
    case Ambulance::PREEMPTED: return "PREEMPTED";
    default:
      assert(false);
      return "ERROR: NO STATE";
  }
}

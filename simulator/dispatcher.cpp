#include "data.hpp"
#include "dispatcher.hpp"
#include "routing.hpp"
#include <iostream>
#include "range/v3/view/map.hpp"
#include "range/v3/view/filter.hpp"
#include "range/v3/view/remove.hpp"
#include "range/v3/view/concat.hpp"
#include "range/v3/view/zip.hpp"
#include "range/v3/view/transform.hpp"
#include "range/v3/range/conversion.hpp"
#include "range/v3/action/sort.hpp"
#include "range/v3/numeric/accumulate.hpp"

using namespace ranges;

// TODO: add a scheduled cleanup method that regularly (every 12/24h?) removes
//       those emergencies that have not been served for that long

simcpp20::event<Time> Dispatcher::cleanup() {
  auto limit = (conf.end_time - conf.start_time).total_seconds();
  do {
    co_await sim.timeout(CLEANUP_INTERVAL);
    Time now = sim.now();
    spdlog::info("[{}] Clean up procedure started", std::to_string(conf.start_time, now));
    for (auto& el_list : waiting_emergencies) {
      auto it = el_list.second.begin();
      while (it != el_list.second.end()) {
        auto e = *it;
        if (now - e->occurring_time > CLEANUP_INTERVAL) {
          spdlog::warn("[{}] Cleaning up emergency {}, waiting too long {}", std::to_string(conf.start_time, now), *e, units::time::to_string(units::time::hour_t(units::time::second_t(now - e->occurring_time))));
          it = el_list.second.erase(it);
        }
        else
          ++it;
      }
    }
  } while (sim.now() < limit);
}

simcpp20::event<Time> Dispatcher::preempted_emergency(std::shared_ptr<Emergency> e) {
  co_await sim.timeout(0);
  serving_emergencies[e->triage].remove(e);
  waiting_emergencies[e->triage].push_back(e);
#ifdef LOGGING
  spdlog::info("[{}] Emergency {} back to dispatcher", std::to_string(conf.start_time, sim.now()), *e);
  size_t waiting = accumulate(waiting_emergencies | views::values | views::transform([](auto& l){ return l.size(); }), size_t(0)),
  serving = accumulate(serving_emergencies | views::values | views::transform([](auto& l){ return l.size(); }), size_t(0));
  Time now = sim.now();
  auto max_waiting_time_red = units::time::second_t(accumulate(waiting_emergencies[Emergency::RED] | views::transform([now](const auto& e) { return now - e->occurring_time; }), 0, [](Time a, Time b) { return std::max(a, b); }));
  auto max_waiting_time_yellow = units::time::second_t(accumulate(waiting_emergencies[Emergency::YELLOW] | views::transform([now](const auto& e) { return now - e->occurring_time; }), 0, [](Time a, Time b) { return std::max(a, b); }));
  auto max_waiting_time_green = units::time::second_t(accumulate(waiting_emergencies[Emergency::GREEN] | views::transform([now](const auto& e) { return now - e->occurring_time; }), 0, [](Time a, Time b) { return std::max(a, b); }));
  auto max_waiting_time_white = units::time::second_t(accumulate(waiting_emergencies[Emergency::WHITE] | views::transform([now](const auto& e) { return now - e->occurring_time; }), 0, [](Time a, Time b) { return std::max(a, b); }));
  spdlog::info("[{}] Dispatcher (requeued {}) currently serving {} emergencies (R: {}, Y: {}, G: {}, W: {}), waiting {} emergencies (R: {}/{}, Y: {}/{}, G: {}/{}, W: {}/{})", std::to_string(conf.start_time, sim.now()), *e,
               serving,
               serving_emergencies[Emergency::RED].size(),
               serving_emergencies[Emergency::YELLOW].size(),
               serving_emergencies[Emergency::GREEN].size(),
               serving_emergencies[Emergency::WHITE].size(),
               waiting,
               waiting_emergencies[Emergency::RED].size(), units::time::to_string(max_waiting_time_red),
               waiting_emergencies[Emergency::YELLOW].size(), units::time::to_string(max_waiting_time_yellow),
               waiting_emergencies[Emergency::GREEN].size(), units::time::to_string(max_waiting_time_green),
               waiting_emergencies[Emergency::WHITE].size(), units::time::to_string(max_waiting_time_white));
#endif
}

simcpp20::event<Time> Dispatcher::new_emergency(std::shared_ptr<Emergency> e) {
#ifdef NDEBUG
  // TODO: do it with the range views
  for (const auto em_list : waiting_emergencies) {
    assert(!any_of(em_list, [e](const auto& p) { return p == e; }));
  }
  for (const auto em_list : serving_emergencies) {
    assert(!any_of(em_list, [e](const auto& p) { return p == e; }));
  }
#endif
  switch (e->triage) {
    case Emergency::RED:
      co_await sim.timeout(30 + conf.dispatcher_call_dist_red(conf.gen));
      break;
    case Emergency::YELLOW:
      co_await sim.timeout(30 + conf.dispatcher_call_dist_yellow(conf.gen));
      break;
    case Emergency::GREEN:
      co_await sim.timeout(30 + conf.dispatcher_call_dist_green(conf.gen));
      break;
    case Emergency::WHITE:
      co_await sim.timeout(30 + conf.dispatcher_call_dist_white(conf.gen));
      break;
    default:
      co_await sim.timeout(0);
      break;
  }
  co_await sim.timeout(0); // just to be sure that is done when everything else at the same timepoint has been executed
  bool served = false;
  // TODO: same management of the RED for the critical YELLOW, to be identified
  if (e->triage == Emergency::RED) {
    std::shared_ptr<Ambulance> a, mv;
    auto ambulances = get_ambulances(e, Ambulance::ALS, DISTANCE_THRESHOLD, TIME_THRESHOLD);
    auto medical_vehicles = get_ambulances(e, Ambulance::MV, DISTANCE_THRESHOLD, TIME_THRESHOLD);
    if (medical_vehicles.size() > 0 && ambulances.size() > 0) {
      // perfect situation, send both
      auto a = ambulances.front().first;
      if (!a->waiting()) {
        assert(a->preemptable(e));
        a->preempt();
      }
      auto mv = medical_vehicles.front().first;
      if (!mv->waiting()) {
        assert(mv->preemptable(e));
        mv->preempt();
      }
      a->assign_pair(e, ambulances.front().second, mv, medical_vehicles.front().second);
      served = true;
    }
    else if (medical_vehicles.size() == 0 && ambulances.size() > 0) {
      // no MV but an ALS, send it
      auto a = ambulances.front().first;
      if (!a->waiting()) {
        assert(a->preemptable(e));
        a->preempt();
      }
      a->assign(e, ambulances.front().second);
      served = true;
    }
    else if (ambulances.size() == 0) {
      ambulances = get_ambulances(e, Ambulance::BLS, DISTANCE_THRESHOLD, TIME_THRESHOLD);
      if (medical_vehicles.size() > 0 && ambulances.size() > 0) {
        // a MV but no ALS, send a BLS
        auto a = ambulances.front().first;
        if (!a->waiting()) {
          assert(a->preemptable(e));
          a->preempt();
        }
        //a->assign(e, ambulances.front().second);
        auto mv = medical_vehicles.front().first;
        if (!mv->waiting()) {
          assert(mv->preemptable(e));
          mv->preempt();
        }
        //mv->assign(e, medical_vehicles.front().second);
        a->assign_pair(e, ambulances.front().second, mv, medical_vehicles.front().second);
        served = true;
      }
      else if (medical_vehicles.size() == 0 and ambulances.size() > 0) {
        auto a = ambulances.front().first;
        if (!a->waiting()) {
          assert(a->preemptable(e));
          a->preempt();
        }
        a->assign(e, ambulances.front().second);
        served = true;
      }
    }
  }
  else if (e->triage == Emergency::YELLOW) {
    std::shared_ptr<Ambulance> a;
    auto ambulances = get_ambulances(e, Ambulance::ALS, DISTANCE_THRESHOLD, TIME_THRESHOLD);
    if (ambulances.size() > 0) {
      auto a = ambulances.front().first;
      if (!a->waiting()) {
        assert(a->preemptable(e));
        a->preempt();
      }
      a->assign(e, ambulances.front().second);
      served = true;
    }
    if (!served) {
      auto ambulances = get_ambulances(e, Ambulance::BLS, DISTANCE_THRESHOLD, TIME_THRESHOLD);
      if (ambulances.size() > 0) {
      auto a = ambulances.front().first;
        if (!a->waiting()) {
          assert(a->preemptable(e));
          a->preempt();
        }
        a->assign(e, ambulances.front().second);
        served = true;
      }
    }
  }
  else if (e->triage == Emergency::GREEN) {
    std::shared_ptr<Ambulance> a;
    auto ambulances = get_ambulances(e, Ambulance::BLS, DISTANCE_THRESHOLD, TIME_THRESHOLD);
    if (ambulances.size() > 0) {
      auto a = ambulances.front().first;
      if (!a->waiting()) {
        assert(a->preemptable(e));
        a->preempt();
      }
      a->assign(e, ambulances.front().second);
      served = true;
    } else {
      ambulances = get_ambulances(e, Ambulance::ALS, DISTANCE_THRESHOLD, TIME_THRESHOLD);
      if (ambulances.size() > 0) {
        auto a = ambulances.front().first;
        if (!a->waiting()) {
          assert(a->preemptable(e));
          a->preempt();
        }
        a->assign(e, ambulances.front().second);
        served = true;
      }
    }
  } else if (e->triage == Emergency::WHITE) {
    std::shared_ptr<Ambulance> a;
    auto ambulances = get_ambulances(e, Ambulance::BLS, DISTANCE_THRESHOLD, TIME_THRESHOLD);
    if (ambulances.size() > 0) {
      auto a = ambulances.front().first;
      if (!a->waiting()) {
        assert(a->preemptable(e));
        a->preempt();
      }
      a->assign(e, ambulances.front().second);
      served = true;
    }
  }
  if (served)
    serving_emergencies[e->triage].push_back(e);
  else
    waiting_emergencies[e->triage].push_back(e);
#ifdef LOGGING
  size_t waiting = accumulate(waiting_emergencies | views::values | views::transform([](auto& l){ return l.size(); }), size_t(0)),
  serving = accumulate(serving_emergencies | views::values | views::transform([](auto& l){ return l.size(); }), size_t(0));
  Time now = sim.now();
  auto max_waiting_time_red = units::time::second_t(accumulate(waiting_emergencies[Emergency::RED] | views::transform([now](const auto& e) { return now - e->occurring_time; }), 0, [](Time a, Time b) { return std::max(a, b); }));
  auto max_waiting_time_yellow = units::time::second_t(accumulate(waiting_emergencies[Emergency::YELLOW] | views::transform([now](const auto& e) { return now - e->occurring_time; }), 0, [](Time a, Time b) { return std::max(a, b); }));
  auto max_waiting_time_green = units::time::second_t(accumulate(waiting_emergencies[Emergency::GREEN] | views::transform([now](const auto& e) { return now - e->occurring_time; }), 0, [](Time a, Time b) { return std::max(a, b); }));
  auto max_waiting_time_white = units::time::second_t(accumulate(waiting_emergencies[Emergency::WHITE] | views::transform([now](const auto& e) { return now - e->occurring_time; }), 0, [](Time a, Time b) { return std::max(a, b); }));
  
  spdlog::info("[{}] Dispatcher (emergency {} {}) currently serving {} emergencies (R: {}, Y: {}, G: {}, W: {}), waiting {} emergencies (R: {}/{}, Y: {}/{}, G: {}/{}, W: {}/{})", std::to_string(conf.start_time, sim.now()), (served ? "served" : "waiting"), *e,
               serving,
               serving_emergencies[Emergency::RED].size(),
               serving_emergencies[Emergency::YELLOW].size(),
               serving_emergencies[Emergency::GREEN].size(),
               serving_emergencies[Emergency::WHITE].size(),
               waiting,
               waiting_emergencies[Emergency::RED].size(), units::time::to_string(max_waiting_time_red),
               waiting_emergencies[Emergency::YELLOW].size(), units::time::to_string(max_waiting_time_yellow),
               waiting_emergencies[Emergency::GREEN].size(), units::time::to_string(max_waiting_time_green),
               waiting_emergencies[Emergency::WHITE].size(), units::time::to_string(max_waiting_time_white));
#endif
}

std::vector<std::pair<std::shared_ptr<Ambulance>, Routing::Segment>> Dispatcher::get_ambulances(std::shared_ptr<Emergency> e, Ambulance::Type t, units::length::kilometer_t d_threshold, units::time::minute_t t_threshold) {
  auto compatible_ambulances = available_ambulances | views::filter([e, d_threshold](auto a) { return (a->waiting() || a->preemptable(e)) && Routing::haversine(e->place, a->current_position()) < d_threshold; }) | views::filter([t](auto a) { return a->type == t; }) | to<std::vector>();
  if (compatible_ambulances.size() == 0)
    return {};
  std::vector<Routing::Segment> result = routing.compute_distances(compatible_ambulances | views::transform([](auto a) { return a->base; }) | to<std::list>, e->place);
  return views::zip(compatible_ambulances, result) | views::filter([t_threshold](const auto& p) { return p.second.duration < t_threshold; }) | to<std::vector> | actions::sort([](const auto& p1, const auto& p2) { return int(p1.first->current_state) < int(p2.first->current_state) || (p1.first->current_state == p2.first->current_state && p1.second.duration < p2.second.duration); });
}

simcpp20::event<Time> Dispatcher::assignable_ambulance(std::shared_ptr<Ambulance> a) {
  co_await sim.timeout(0);
  if (a->assigned()) // already taken
    co_return;
#ifdef LOGGING
  spdlog::debug("[{}] Dispatcher ambulance {} available for assignment", std::to_string(conf.start_time, sim.now()), *a);
#endif
  if (a->type != Ambulance::MV) {
    auto compatible_emergencies = views::concat(waiting_emergencies[Emergency::RED], waiting_emergencies[Emergency::YELLOW]) | views::filter([a](const auto& e) { return Routing::haversine(e->place, a->current_position()) < DISTANCE_THRESHOLD; }) | to<std::vector>;
    if (compatible_emergencies.size() == 0) {
      compatible_emergencies = views::concat(waiting_emergencies[Emergency::GREEN], waiting_emergencies[Emergency::WHITE]) | views::filter([a](const auto& e) { return Routing::haversine(e->place, a->current_position()) < DISTANCE_THRESHOLD; }) | to<std::vector>;
      if (compatible_emergencies.size() == 0)
        co_return;
    }
    auto now = sim.now();
    auto t_threshold = TIME_THRESHOLD;
    std::vector<Routing::Segment> routes = routing.compute_distances({ a->current_position() }, compatible_emergencies | views::transform([](auto e) { return e->place; }) | to<std::list>);
    
    auto result = views::zip(compatible_emergencies, routes) | views::filter([t_threshold](const auto& p) { return p.second.duration < t_threshold; }) | to<std::vector> | actions::sort([](const auto& p1, const auto& p2) { return int(p1.first->triage) < int(p2.first->triage) || (p1.first->triage == p2.first->triage && p1.first->occurring_time < p2.first->occurring_time) || (p1.first->triage == p2.first->triage && p1.first->occurring_time == p2.first->occurring_time &&  p1.second.duration < p2.second.duration); });
    if (result.size() == 0)
      co_return;
    std::shared_ptr<Emergency> e;
    Routing::Segment s;
    std::tie(e, s) = result.front();
    if (!a->waiting()) {
      assert(a->preemptable(e));
      a->preempt();
    }
    waiting_emergencies[e->triage].remove(e);
    serving_emergencies[e->triage].push_back(e);
    //a->assign(e, s);
    if (e->triage == Emergency::RED) {
      auto medical_vehicles = get_ambulances(e, Ambulance::MV, DISTANCE_THRESHOLD, TIME_THRESHOLD);
      if (medical_vehicles.size() > 0) {
        auto mv = medical_vehicles.front().first;
        if (medical_vehicles.front().second.duration < s.duration || medical_vehicles.front().second.duration < units::time::second_t(1.1 * SERVICE_TIME_THRESHOLD)) {
          if (!mv->waiting()) {
            assert(mv->preemptable(e));
            mv->preempt();
          }
          a->assign_pair(e, s, mv, medical_vehicles.front().second);
        }
      } else {
        a->assign(e, s);
      }
    } else
      a->assign(e, s);
#ifdef LOGGING
    size_t waiting = accumulate(waiting_emergencies | views::values | views::transform([](auto& l){ return l.size(); }), size_t(0)),
    serving = accumulate(serving_emergencies | views::values | views::transform([](auto& l){ return l.size(); }), size_t(0));
    auto max_waiting_time_red = units::time::second_t(accumulate(waiting_emergencies[Emergency::RED] | views::transform([now](const auto& e) { return now - e->occurring_time; }), 0, [](Time a, Time b) { return std::max(a, b); }));
    auto max_waiting_time_yellow = units::time::second_t(accumulate(waiting_emergencies[Emergency::YELLOW] | views::transform([now](const auto& e) { return now - e->occurring_time; }), 0, [](Time a, Time b) { return std::max(a, b); }));
    auto max_waiting_time_green = units::time::second_t(accumulate(waiting_emergencies[Emergency::GREEN] | views::transform([now](const auto& e) { return now - e->occurring_time; }), 0, [](Time a, Time b) { return std::max(a, b); }));
    auto max_waiting_time_white = units::time::second_t(accumulate(waiting_emergencies[Emergency::WHITE] | views::transform([now](const auto& e) { return now - e->occurring_time; }), 0, [](Time a, Time b) { return std::max(a, b); }));
    spdlog::info("[{}] Dispatcher (ambulance {} for {}) currently serving {} emergencies (R: {}, Y: {}, G: {}, W: {}), waiting {} emergencies (R: {}/{}, Y: {}/{}, G: {}/{}, W: {}/{})", std::to_string(conf.start_time, sim.now()), *a, *e,
                 serving,
                 serving_emergencies[Emergency::RED].size(),
                 serving_emergencies[Emergency::YELLOW].size(),
                 serving_emergencies[Emergency::GREEN].size(),
                 serving_emergencies[Emergency::WHITE].size(),
                 waiting,
                 waiting_emergencies[Emergency::RED].size(), units::time::to_string(max_waiting_time_red),
                 waiting_emergencies[Emergency::YELLOW].size(), units::time::to_string(max_waiting_time_yellow),
                 waiting_emergencies[Emergency::GREEN].size(), units::time::to_string(max_waiting_time_green),
                 waiting_emergencies[Emergency::WHITE].size(), units::time::to_string(max_waiting_time_white));
#endif
  }
}

void Dispatcher::ambulance_available(std::shared_ptr<Ambulance> a) {
#ifdef NDEBUG
  assert(!std::any_of(available_ambulances.begin(), available_ambulances.end(), [a](const auto& p) { return p == a; } ));
#endif
  available_ambulances.push_back(a);
  assignable_ambulance(a);
}

simcpp20::event<Time> Dispatcher::ambulance_unavailable(std::shared_ptr<Ambulance> a) {
//  auto it = std::find_if(available_ambulances.begin(), available_ambulances.end(), [a](const auto& p) { return p == a; });
#ifdef NDEBUG
  assert(it != available_ambulances.end());
  assert(a->current_state != Ambulance::UNAVAILABLE);
#endif
  available_ambulances.remove(a);
  if (a->waiting()) {
    auto ev = sim.event();
    ev.trigger();
    return ev;
  } else {
    return a->rescue_finished();
  }
}

void Dispatcher::emergency_served(std::shared_ptr<Emergency> e) {
#ifdef NDEBUG
  for (const auto em_list : serving_emergencies) {
    assert(any_of(em_list, [e](const auto& p) { return p == e; }));
  }
  for (const auto em_list : waiting_emergencies) {
    assert(!any_of(em_list, [e](const auto& p) { return p == e; }));
  }
#endif
  serving_emergencies[e->triage].remove(e);
}

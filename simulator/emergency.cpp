#include "data.hpp"
#include "emergency.hpp"
#include "routing.hpp"
#include "helpers.hpp"
#include "dispatcher.hpp"
#include <iostream>

std::istream &operator>>(std::istream &is, Emergency::Code &code)
{
  std::string tmp;
  is >> tmp;
  if (tmp == "RED")
    code = Emergency::Code::RED;
  else if (tmp == "YELLOW")
    code = Emergency::Code::YELLOW;
  else if (tmp == "GREEN")
    code = Emergency::Code::GREEN;
  else if (tmp == "WHITE")
    code = Emergency::Code::WHITE;
  else if (tmp == "BLACK")
    code = Emergency::Code::BLACK;
  else
    throw std::logic_error("Triage type (" + tmp + ") not recognized");
  return is;
}

std::istream &operator>>(std::istream &is, Emergency &e)
{
  std::string tmp, date, time;
  is >> e.id >> e.municipality >> e.triage >> e.place >> tmp >> date >> time;
  tmp = date + " " + time;
  boost::trim(tmp);
  e.timestamp = pt::time_from_string(tmp);
  std::getline(is, tmp);
  boost::trim(tmp);
  if (tmp != "") {
    e.needs_hospital = true;
    std::istringstream read_is(tmp);
    read_is >> e.needed_hospital >> e.actual_hospital;
  } else {
    e.needs_hospital = false;
  }
  return is;
}

std::ostream& operator<<(std::ostream &os, const Emergency::Code& c) {
  switch (c) {
    case Emergency::Code::RED:
      os << "RED";
      break;
    case Emergency::Code::YELLOW:
      os << "YELLOW";
      break;
    case Emergency::Code::GREEN:
      os << "GREEN";
      break;
    case Emergency::Code::WHITE:
      os << "WHITE";
      break;
    case Emergency::Code::BLACK:
      os << "BLACK";
      break;
  }
  return os;
}

simcpp20::event<Time> Emergency::generate()
{
  current_state = SCHEDULED;
  // put the emergency in the event queue at the right time
  occurring_time = (timestamp - conf.start_time).total_seconds() - sim.now();
  start_serving_time = reaching_time = at_hospital_time = std::numeric_limits<Time>::max();  
  co_await sim.timeout(occurring_time);
#ifdef LOGGING
  spdlog::info("[{}] Emergency {} happens at {}", std::to_string(conf.start_time, sim.now()), *this, std::to_string(place));
#endif
  co_await dispatcher.new_emergency(emergencies[index]);
}

void Emergency::source(std::istream &is, simcpp20::simulation<Time> &sim, config &conf, Dispatcher& dispatcher)
{
  pt::ptime min_time, max_time;
  while (!is.eof())
  {
    auto e = std::make_shared<Emergency>(sim, conf, dispatcher);
    try
    {
      is >> *e;
    }
    catch (std::exception &e)
    {
      spdlog::error("An exception occurred while reading emergencies file {}", e.what());
      continue;
    }

    // avoid generating emergencies beyond the times (if provided)
    if ((conf.start_time.is_special() || e->timestamp >= conf.start_time) && (conf.end_time.is_special() || e->timestamp <= conf.end_time))
    {
      if (min_time.is_not_a_date_time() || min_time > e->timestamp)
        min_time = e->timestamp;
      if (max_time.is_not_a_date_time() || max_time < e->timestamp)
        max_time = e->timestamp;
      emergencies.push_back(e);
      e->index = emergencies.size() - 1;
      e->generate();
    } 
  }
  spdlog::info(min_time);
  spdlog::info(max_time);
  // update start and end time if not provided or too loose
  if (conf.start_time.is_not_a_date_time())
    conf.start_time = pt::ptime(min_time.date());
  if (conf.end_time.is_not_a_date_time()) {
    conf.end_time = pt::ptime(max_time.date(), pt::hours(23) + pt::minutes(59) + pt::seconds(59));
  }
  spdlog::info("Simulation horizon {} - {}", to_simple_string(conf.start_time), to_simple_string(conf.end_time));
}

std::vector<std::shared_ptr<Emergency>> Emergency::emergencies;

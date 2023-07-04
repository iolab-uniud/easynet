#pragma once

#include "data.hpp"
#include "emergency.hpp"
#include "hospital.hpp"
#include "ambulance.hpp"
#include <boost/algorithm/string.hpp>
#include <iostream>
#include <functional>
#include "SQLiteCpp/SQLiteCpp.h"
#include "spdlog/fmt/ostr.h"
#include "termcolor/termcolor.hpp"
#include <unistd.h>
#include <cstdlib>

namespace std {

inline std::string to_string(const pt::ptime& start_time, Time t) {
  return to_simple_string(start_time + pt::seconds(t));
}

inline std::string to_string(const Coordinate& c) {
  return "(" + std::to_string(c.lat.__value) + ", " + std::to_string(c.lon.__value) + ")";
}

inline std::string to_string(const Emergency::Code& c) {
  std::ostringstream oss;
  oss << c;
  return oss.str();
}

}

class SimulationData {
public:
  static void set_database(std::string db_filename);
  static void log_rescue(const Emergency& e, const Ambulance& a, const pt::ptime& start_time);
  static void log_ambulance(const Ambulance& a, const Emergency& e, Time now, const pt::ptime& start_time);
  static void log_ambulance(const Ambulance& a, Time now, const pt::ptime& start_time);
protected:
  static std::unique_ptr<SQLite::Database> db;
};

template <typename OStream>
OStream& operator<<(OStream &os, const Hospital& h) {
  os << h.id << "[" << h.description << ", " << h.type << "]";
  return os;
}

template <typename OStream>
OStream& operator<<(OStream &os, const Emergency& e) {
  if (colored)
    os << termcolor::colorize << termcolor::bold;
  switch (e.triage) {
    case Emergency::Code::RED:
      os << termcolor::bright_red;
      break;
    case Emergency::Code::YELLOW:
      os << termcolor::bright_yellow;
      break;
    case Emergency::Code::GREEN:
      os << termcolor::bright_green;
      break;
    case Emergency::Code::WHITE:
      os << termcolor::bright_white;
      break;
    case Emergency::Code::BLACK:
      os << termcolor::grey;
      break;
  }
  os << e.id << "[" << e.municipality << ", " << e.triage << ", " << e.needed_hospital << "]";
  if (colored)
    os << termcolor::reset;
  return os;
}

template <typename OStream>
OStream& operator<<(OStream &os, const Ambulance& a) {
  if (colored)
    os << termcolor::colorize << termcolor::bold;
  switch (a.type) {
    case Ambulance::Type::ALS:
      os << termcolor::bright_magenta;
      break;
    case Ambulance::Type::BLS:
      os << termcolor::bright_cyan;
      break;
    case Ambulance::Type::MV:
      os << termcolor::bright_white;
      break;
  }
  os << a.id << "[" << a.description << ", " << a.type << "]";
  if (colored)
    os << termcolor::reset;
  return os;
}

inline bool compatible(Ambulance::Type t, Emergency::Code c) {
  if (c == Emergency::Code::RED || c == Emergency::Code::YELLOW)
    return t == Ambulance::Type::ALS;
  else
    return t == Ambulance::Type::BLS;
}

extern units::length::kilometer_t DISTANCE_THRESHOLD;
extern units::time::minute_t TIME_THRESHOLD;

const Time SERVICE_TIME_THRESHOLD = 18*60;
const Time DISCHARGING_TIME = 3 * 60;
const Time CLEANING_TIME = 10 * 60;

const Time CLEANUP_INTERVAL = 12 * 60 * 60;

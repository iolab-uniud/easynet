// Copyright © 2022 Luca Di Gaspero.
// Licensed under the MIT license. See the LICENSE file for details.

#include <cstdio>
#include <iostream>
#include <fstream>
#include <random>
#include <locale>
#include <iomanip>
#include <chrono>
#include <vector>

#include "simcpp20/simcpp20.hpp"
#include "simcpp20/resource.hpp"
#include "data.hpp"
#include "emergency.hpp"
#include "ambulance.hpp"
#include "hospital.hpp"
#include "dispatcher.hpp"
#include "helpers.hpp"

#include <boost/program_options.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "spdlog/spdlog.h"
#include "spdlog/async.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_sinks.h"

#include "indicators/progress_bar.hpp"

namespace po = boost::program_options;
namespace pt = boost::posix_time;

simcpp20::event<Time> manage_progress_bar(simcpp20::simulation<Time> &sim, config &conf)
{
  Time amount = (conf.end_time - conf.start_time).total_seconds();
  using namespace indicators;
  ProgressBar bar{
    option::BarWidth{50},
    option::Start{"["},
    option::Fill{"="},
    option::Lead{">"},
    option::Remainder{" "},
    option::End{"]"},
    option::PostfixText{"Day "},
    option::MaxProgress{amount},
    option::ForegroundColor{Color::green},
    option::ShowElapsedTime{true},
    option::ShowRemainingTime{true},
    option::ShowPercentage{true},
    option::FontStyles{std::vector<FontStyle>{FontStyle::bold}},
    option::Stream{std::cerr}};
  // this is needed to handle the last update
  while (sim.now() < amount - 1) {
    auto current_day = (conf.start_time + pt::seconds(sim.now())).date();
    bar.set_option(option::PostfixText{std::string("Day ") + to_simple_string(current_day)});
    bar.set_progress(sim.now());
    co_await sim.timeout(std::min<Time>(amount - 1 - sim.now(), std::min<Time>(amount / 100, 84600)));
  }
  bar.set_progress(amount);
}

int main(int argc, const char *argv[])
{
  simcpp20::simulation<Time> sim;
  
  // default values
  config conf{
    .dispatcher_call_dist_red = std::exponential_distribution<>{1. / 253},
    .dispatcher_call_dist_yellow = std::exponential_distribution<>{1. / 367},
    .dispatcher_call_dist_green = std::exponential_distribution<>{1. / 688},
    .dispatcher_call_dist_white = std::exponential_distribution<>{1. / 1188},
    .treatment_duration_dist = std::exponential_distribution<>{1. / 300},
    .preemptable = true
  };
  
  unsigned long seed = 42;
  std::string start_time, end_time;
  std::string log_filename, data_filename;
  bool progress = false, no_log = false, not_preemptable = false;
  double dt, tt;
  double red_call_lambda, yellow_call_lambda, green_call_lambda, white_call_lambda;
  po::options_description desc("Command line options");
  desc.add_options()("help,?", "print usage message")
  ("emergencies,e", po::value(&conf.emergencies_filename), "Emergencies file")
  ("ambulances,a", po::value(&conf.ambulances_filename), "Ambulances file")
  ("hospitals,h", po::value(&conf.hospitals_filename), "Hospital file")
  ("routing,r", po::value(&conf.osrm_filename), "Routing data file(s)")
  ("seed,s", po::value(&seed), "Random seed")
  ("start-time", po::value(&start_time), "Simulation start time")
  ("end-time", po::value(&end_time), "Simulation end time")
  ("progress-bar,p", po::bool_switch(&progress), "Show progress bar")
  ("no-log,n", po::bool_switch(&no_log), "Disable log")
  ("colored-log,c", po::bool_switch(&colored), "Show colored log")
  ("log-file,l", po::value(&log_filename), "Log file")
  ("data-file,d", po::value(&data_filename), "Simulation SQLite filename")
  ("rescue-distance-threshold,dt", po::value(&dt), "Rescue distance threshold (in km)")
  ("rescue-time-threshold,tt", po::value(&tt), "Rescue time threshold (in minutes)")
  ("red-call-lambda,rcl", po::value(&red_call_lambda), "Lambda value for dispatching red calls")
  ("yellow-call-lambda,ycl", po::value(&yellow_call_lambda), "Lambda value for dispatching yellow calls")
  ("green-call-lambda,gcl", po::value(&green_call_lambda), "Lambda value for dispatching green calls")
  ("white-call-lambda,wcl", po::value(&white_call_lambda), "Lambda value for dispatching white calls")
  ("not-preemptable", po::bool_switch(&not_preemptable), "Non preemtable events");
  
  std::mt19937 rd(seed);
  // this will use hardware entropy
  //std::random_device rd;
  conf.gen = std::default_random_engine{rd()};
  
  // Parse command line arguments
  po::variables_map vm;
  po::store(po::command_line_parser(argc, argv).options(desc).run(), vm);
  po::notify(vm);
  if (vm.count("help") || !vm.count("emergencies") || !vm.count("ambulances") || !vm.count("routing") || !vm.count("hospitals")) {
    std::cerr << desc << "\n";
    return 1;
  }
  if (vm.count("start-time")) {
    conf.start_time = pt::time_from_string(start_time);
  }
  if (vm.count("end-time")) {
    conf.end_time = pt::time_from_string(end_time);
  }
  if (vm.count("rescue-distance-threshold")) {
    DISTANCE_THRESHOLD = units::length::kilometer_t(dt);
  }
  if (vm.count("rescue-time-threshold")) {
    TIME_THRESHOLD = units::time::minute_t(tt);
  }
  conf.preemptable = !not_preemptable;
  
  if (no_log) {
    spdlog::set_level(spdlog::level::off);
  } else {
    if (vm.count("log-file")) {
      auto async_file = spdlog::basic_logger_mt<spdlog::async_factory>("simulator", log_filename);
      spdlog::set_default_logger(async_file);
    } else {
      auto console = spdlog::stdout_logger_mt("simulator");
      spdlog::set_default_logger(console);
    }
    spdlog::set_level(spdlog::level::info);
  }
  spdlog::set_pattern("%v");
  if (vm.count("red-call-lambda")) {
    conf.dispatcher_call_dist_red = std::exponential_distribution<>(1.0 / red_call_lambda);
  }
  if (vm.count("yellow-call-lambda")) {
    conf.dispatcher_call_dist_yellow = std::exponential_distribution<>(1.0 / yellow_call_lambda);
  }
  if (vm.count("green-call-lambda")) {
    conf.dispatcher_call_dist_green = std::exponential_distribution<>(1.0 / green_call_lambda);
  }
  if (vm.count("white-call-lambda")) {
    conf.dispatcher_call_dist_white = std::exponential_distribution<>(1.0 / white_call_lambda);
  }
  
  std::ifstream is;
  
  osrm::EngineConfig config;
  config.storage_config = {conf.osrm_filename};
  config.use_shared_memory = false;
  config.algorithm = osrm::EngineConfig::Algorithm::CH;
  Routing routing(config);
  SimulationData::set_database(!data_filename.empty() ? data_filename : "default.sqlite3.db");
  
  Dispatcher dispatcher(sim, conf, routing);
  
  is.open(conf.emergencies_filename);
  if (!is)
  {
    throw std::logic_error("Could not open emergencies file " + conf.emergencies_filename);
    return -1;
  }
  Emergency::source(is, sim, conf, dispatcher);
  is.close();
  
  is.open(conf.ambulances_filename);
  if (!is)
  {
    throw std::logic_error("Could not open ambulances file " + conf.ambulances_filename);
    return -1;
  }
  Ambulance::source(is, sim, conf, dispatcher, routing);
  is.close();
  
  is.open(conf.hospitals_filename);
  if (!is)
  {
    throw std::logic_error("Could not open hospitals file " + conf.hospitals_filename);
    return -1;
  }
  Hospital::source(is);
  is.close();
  
  Time limit = (conf.end_time - conf.start_time).total_seconds();
  
#ifdef LOGGING
  spdlog::info("[{}] Simulation started", std::to_string(conf.start_time, sim.now()));
#endif
  
  if (progress)
    manage_progress_bar(sim, conf);
  
  //  manage_emergencies(sim, conf, emergencies, available_ambulances, hospitals);
  
  //sim.run_until(limit);
  sim.run();
#ifdef LOGGING
  spdlog::info("[{}] Simulation ended", std::to_string(conf.start_time, sim.now()));
#endif
  
  //  while (emergencies.size() > 0)
  //  {
  //    auto e = emergencies.get();
  //    if (e.pending())
  //    {
  //      spdlog::warn("[{}] Emergency {} was not served", timestamp(conf.start_time, sim.now()), e.value()->id);
  //    }
  //  }
  
  return 0;
}

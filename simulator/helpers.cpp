#include "data.hpp"
#include "helpers.hpp"

bool colored = false;
units::length::kilometer_t DISTANCE_THRESHOLD = units::length::kilometer_t(20.0);
units::time::minute_t TIME_THRESHOLD(45.0);

void SimulationData::set_database(std::string db_filename) {    
  SimulationData::db = std::make_unique<SQLite::Database>(db_filename, SQLite::OPEN_READWRITE|SQLite::OPEN_CREATE);
  SQLite::Transaction transaction(*SimulationData::db);
  db->exec("DROP TABLE IF EXISTS rescue");
  db->exec("CREATE TABLE IF NOT EXISTS rescue (emergency VARCHAR(255) NOT NULL, ambulance VARCHAR(255) NOT NULL, hospital VARCHAR(32), triage VARCHAR(10) NOT NULL, call DATETIME NOT NULL, start DATETIME NOT NULL, at_emergency DATETIME NOT NULL, at_hospital DATETIME, PRIMARY KEY (emergency, ambulance))");
  db->exec("DROP TABLE IF EXISTS ambulance_event");
  db->exec("CREATE TABLE IF NOT EXISTS ambulance_event (ambulance VARCHAR(255) NOT NULL, emergency VARCHAR(255), state VARCHAR(255) NOT NULL, time DATETIME NOT NULL)");
  transaction.commit();
}

void SimulationData::log_rescue(const Emergency& e, const Ambulance& a, const pt::ptime& start_time) {
  try {
    if (!SimulationData::db)
      return;
    SQLite::Statement query(*SimulationData::db, "INSERT INTO rescue VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    SQLite::Transaction transaction(* SimulationData::db);
    query.bind(1, e.id);
    query.bind(2, a.id);
    if (e.needs_hospital)
      query.bind(3, e.assigned_hospital->id);
    else
      query.bind(3, nullptr);
    query.bind(4, std::to_string(e.triage));
    query.bind(5, std::to_string(start_time, e.occurring_time));
    query.bind(6, std::to_string(start_time, e.start_serving_time));
    query.bind(7, std::to_string(start_time, e.reaching_time));
    if (e.needs_hospital)
      query.bind(8, std::to_string(start_time, e.at_hospital_time));
    else
      query.bind(8, nullptr);
    query.exec();
    transaction.commit();
  } catch (std::exception& ex) {
    std::cerr << "ERROR in DB logging (rescue) " << ex.what() << std::endl;
    std::cerr << e.id << "/" << a.id << std::endl;
  }
}

void SimulationData::log_ambulance(const Ambulance& a, const Emergency& e, Time now, const pt::ptime& start_time) {
  try {
    if (!SimulationData::db)
      return;
    SQLite::Statement query(*SimulationData::db, "INSERT INTO ambulance_event VALUES (?, ?, ?, ?)");
    SQLite::Transaction transaction(* SimulationData::db);
    query.bind(1, a.id);
    query.bind(2, e.id);
    query.bind(3, std::to_string(a.current_state));
    query.bind(4, std::to_string(start_time, now));
    query.exec();
    transaction.commit();
  } catch (std::exception& ex) {
    std::cerr << "ERROR in DB logging (ambulance) " << ex.what() << std::endl;
    std::cerr << e.id << "/" << a.id << std::endl;
  }
}

void SimulationData::log_ambulance(const Ambulance& a, Time now, const pt::ptime& start_time) {
  try {
    if (!SimulationData::db)
      return;
    SQLite::Statement query(*SimulationData::db, "INSERT INTO ambulance_event VALUES (?, NULL, ?, ?)");
    SQLite::Transaction transaction(* SimulationData::db);
    query.bind(1, a.id);
    query.bind(2, std::to_string(a.current_state));
    query.bind(3, std::to_string(start_time, now));
    query.exec();
    transaction.commit();
  } catch (std::exception& ex) {
    std::cerr << "ERROR in DB logging (ambulance) " << ex.what() << std::endl;
    std::cerr << a.id << std::endl;
  }
}

std::unique_ptr<SQLite::Database> SimulationData::db;

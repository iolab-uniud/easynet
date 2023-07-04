#pragma once

#include "osrm/osrm.hpp"
#include "osrm/engine_config.hpp"
#include "osrm/coordinate.hpp"
#include <list>
#include <vector>
#include "units.h"

typedef osrm::util::FloatCoordinate Coordinate;

// specialization of hash for coordinates
template<>
struct std::hash<Coordinate>
{
  std::size_t operator()(Coordinate const& c) const noexcept
  {
    std::size_t h1 = std::hash<double>{}(c.lon.__value), h2 = std::hash<double>{}(c.lat.__value);
    return h1 ^ (h2 << 1);
  }
};

class Routing {
public:
  struct Segment {
    Coordinate start_point, end_point;
    // duration is expressed in minutes
    units::time::minute_t duration;
    // distance is expressed in km
    units::length::kilometer_t distance;
    // speed is expressed in km/h
    units::velocity::kilometers_per_hour_t speed;
    bool on_highway;
  };
  
  Routing(osrm::EngineConfig config) : osrm{config} {}
  
  static units::length::kilometer_t haversine(const Coordinate& c1, const Coordinate& c2);
  
  std::vector<Segment> compute_distances(const std::list<Coordinate>& start_points, const std::list<Coordinate>& end_points);
  
  inline std::vector<Segment> compute_distances(const Coordinate& start_point, const std::list<Coordinate>& end_points)
  {
    return compute_distances(std::list<Coordinate>({ start_point }), end_points);
  }
  
  inline std::vector<Segment> compute_distances(const std::list<Coordinate>& start_points, Coordinate& end_point)
  {
    return compute_distances(start_points, std::list<Coordinate>({ end_point }));
  }
  
  inline Segment compute_distances(const Coordinate& start_point, const Coordinate& end_point)
  {
    return compute_distances(std::list<Coordinate>({ start_point }), std::list<Coordinate>({ end_point })).front();
  }
  
  std::list<Segment> compute_route(const Coordinate& start_point, const Coordinate& end_point);
  
protected:
  osrm::OSRM osrm;
  // TODO: possibly cache results to avoid recomputing
  // std::map<std::pair<std::size_t, std::size_t>, Result> cached_;
};

std::istream &operator>>(std::istream &is, Coordinate &c);

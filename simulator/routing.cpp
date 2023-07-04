#include "routing.hpp"
#include <cmath>
#include "data.hpp"
#include "spdlog/spdlog.h"

#include "osrm/json_container.hpp"

#include "osrm/route_parameters.hpp"
#include "osrm/table_parameters.hpp"
#include "osrm/status.hpp"

const units::length::kilometer_t rad = units::length::kilometer_t(6371.0);

units::length::kilometer_t Routing::haversine(const Coordinate& c1, const Coordinate& c2)
{
  float lat1, lat2, slat, slon;
  // distance between latitudes
  // and longitudes
  double dLat = (c2.lat.__value - c1.lat.__value) *
                M_PI / 180.0;
  double dLon = (c2.lon.__value - c1.lon.__value) *
                M_PI / 180.0;

  // convert to radians
  lat1 = (c1.lat.__value) * M_PI / 180.0;
  lat2 = (c2.lat.__value) * M_PI / 180.0;
  slat = sin(dLat / 2.0);
  slon = sin(dLon / 2.0);

  // apply formulae
  double a = slat * slat + slon * slon * cos(lat1) * cos(lat2);

  return rad * 2.0 * asin(sqrt(a));
}

std::vector<Routing::Segment> Routing::compute_distances(const std::list<Coordinate>& start_points, const std::list<Coordinate>& end_points)
{
  std::vector<Routing::Segment> results;
      
  osrm::TableParameters params;
  size_t current = 0;
  for (const auto & p : start_points)
  {
    params.coordinates.emplace_back(p);
    params.sources.push_back(current++);
  }
  for (const auto & p : end_points)
  {
    params.coordinates.emplace_back(p);
    params.destinations.push_back(current++);
  }
  params.annotations = osrm::TableParameters::AnnotationsType::All;
  params.fallback_coordinate_type = osrm::TableParameters::FallbackCoordinateType::Snapped;
  osrm::engine::api::ResultT result = osrm::json::Object();
  const auto status = osrm.Table(params, result);
  if (status != osrm::Status::Ok)
  {
    spdlog::error("Error computing table routing ({}) {}", result.get<osrm::json::Object>().values["code"].get<osrm::json::String>().value, result.get<osrm::json::Object>().values["message"].get<osrm::json::String>().value);
    return {};
  }
  
  auto computed_durations = result.get<osrm::json::Object>().values.at("durations").get<osrm::json::Array>();
  auto computed_distances = result.get<osrm::json::Object>().values.at("distances").get<osrm::json::Array>();
  for (size_t s = 0; s < params.sources.size(); s++)
  {
    auto computed_durations_s = computed_durations.values[s].get<osrm::json::Array>().values;
    auto computed_distances_s = computed_distances.values[s].get<osrm::json::Array>().values;
    for (size_t d = 0; d < params.destinations.size(); d++)
    {
      units::time::second_t duration = units::time::second_t(computed_durations_s[d].get<osrm::json::Number>().value);
      units::length::meter_t distance = units::length::meter_t(computed_distances_s[d].get<osrm::json::Number>().value);
      size_t s_index = params.sources[s], d_index = params.destinations[d];
      results.emplace_back(Segment{ params.coordinates[s_index], params.coordinates[d_index], duration, distance, distance / duration, false });
    }
  }
  
  return results;
}

//std::list<Routing::Segment> Routing::compute_route(const Coordinate& start_point, const Coordinate& end_point)
//{
//  osrm::RouteParameters params;
//  params.steps = true;
//  params.alternatives = false;
//  params.annotations = true;
//  params.annotations_type = osrm::RouteParameters::AnnotationsType::Duration | osrm::RouteParameters::AnnotationsType::Distance;
//  params.overview = osrm::RouteParameters::OverviewType::Full;
//  params.geometries = osrm::RouteParameters::GeometriesType::GeoJSON;
//  params.coordinates.emplace_back(start_point);
//  params.coordinates.emplace_back(end_point);
//  osrm::engine::api::ResultT result = osrm::json::Object();
//
//  auto status = osrm.Route(params, result);
//  if (status != osrm::Status::Ok)
//  {
//    spdlog::error("Error computing route ({}) {}",
//                  result.get<osrm::json::Object>().values["code"].get<osrm::json::String>().value,
//                  result.get<osrm::json::Object>().values["message"].get<osrm::json::String>().value);
//    return {};
//  }
//  // get the steps of the route
//  auto legs = result.get<osrm::json::Object>().values.at("routes").get<osrm::json::Array>().values.at(0).get<osrm::json::Object>().values.at("legs").get<osrm::json::Array>().values;
//  auto durations = legs.at(0).get<osrm::json::Object>().values.at("annotation").get<osrm::json::Object>().values.at("duration").get<osrm::json::Array>().values;
//  auto distances = legs.at(0).get<osrm::json::Object>().values.at("annotation").get<osrm::json::Object>().values.at("distance").get<osrm::json::Array>().values;
//  auto geometries = result.get<osrm::json::Object>().values.at("routes").get<osrm::json::Array>().values.at(0).get<osrm::json::Object>().values.at("geometry").get<osrm::json::Object>().values.at("coordinates").get<osrm::json::Array>().values;
//  assert(geometries.size() == durations.size() + 1);
//
//  std::list<Routing::Segment> route;
//  for (size_t i = 0; i < durations.size(); ++i) {
//    Coordinate s_location{ osrm::util::FloatLongitude{geometries[i].get<osrm::json::Array>().values.at(0).get<osrm::json::Number>().value}, osrm::util::FloatLatitude{geometries[i].get<osrm::json::Array>().values.at(1).get<osrm::json::Number>().value }};
//    Coordinate e_location{
//      osrm::util::FloatLongitude{geometries[i + 1].get<osrm::json::Array>().values.at(0).get<osrm::json::Number>().value}, osrm::util::FloatLatitude{geometries[i + 1].get<osrm::json::Array>().values.at(1).get<osrm::json::Number>().value }};
//
//    units::time::second_t duration = units::time::second_t(durations[i].get<osrm::json::Number>().value);
//    units::length::meter_t distance = units::length::meter_t(distances[i].get<osrm::json::Number>().value);
//
//    route.emplace_back(Routing::Segment{ s_location, e_location,
//           duration, distance, distance / duration, false });
//  }
//
//  return route;
//}

std::list<Routing::Segment> Routing::compute_route(const Coordinate& start_point, const Coordinate& end_point)
{
  osrm::RouteParameters params;
  params.steps = true;
  params.alternatives = false;
//  params.annotations = true;
//  params.annotations_type = osrm::RouteParameters::AnnotationsType::Duration | osrm::RouteParameters::AnnotationsType::Distance;
  params.overview = osrm::RouteParameters::OverviewType::False;
  params.geometries = osrm::RouteParameters::GeometriesType::GeoJSON;
  params.coordinates.emplace_back(start_point);
  params.coordinates.emplace_back(end_point);
  osrm::engine::api::ResultT result = osrm::json::Object();

  auto status = osrm.Route(params, result);
  if (status != osrm::Status::Ok)
  {
    spdlog::error("Error computing route ({}) {}",
                  result.get<osrm::json::Object>().values["code"].get<osrm::json::String>().value,
                  result.get<osrm::json::Object>().values["message"].get<osrm::json::String>().value);
    return {};
  }
  // get the steps of the route
  auto legs = result.get<osrm::json::Object>().values.at("routes").get<osrm::json::Array>().values.at(0).get<osrm::json::Object>().values.at("legs").get<osrm::json::Array>().values;
//  auto durations = legs.at(0).get<osrm::json::Object>().values.at("annotation").get<osrm::json::Object>().values.at("duration").get<osrm::json::Array>().values;
//  auto distances = legs.at(0).get<osrm::json::Object>().values.at("annotation").get<osrm::json::Object>().values.at("distance").get<osrm::json::Array>().values;
//  auto geometries = result.get<osrm::json::Object>().values.at("routes").get<osrm::json::Array>().values.at(0).get<osrm::json::Object>().values.at("geometry").get<osrm::json::Object>().values.at("coordinates").get<osrm::json::Array>().values;
//
//    .at(0).get<osrm::json::Object>().values.at("maneuver").get<osrm::json::Object>().values.at("type").get<osrm::json::String>().value;

//  assert(geometries.size() == durations.size() + 1);
//
  std::list<Routing::Segment> route;
//  for (size_t i = 0; i < durations.size(); ++i)
//  {
//    Coordinate s_location{ osrm::util::FloatLongitude{geometries[i].get<osrm::json::Array>().values.at(0).get<osrm::json::Number>().value}, osrm::util::FloatLatitude{geometries[i].get<osrm::json::Array>().values.at(1).get<osrm::json::Number>().value }};
//    Coordinate e_location{
//      osrm::util::FloatLongitude{geometries[i + 1].get<osrm::json::Array>().values.at(0).get<osrm::json::Number>().value}, osrm::util::FloatLatitude{geometries[i + 1].get<osrm::json::Array>().values.at(1).get<osrm::json::Number>().value }};
//
//    units::time::second_t duration = units::time::second_t(durations[i].get<osrm::json::Number>().value);
//    units::length::meter_t distance = units::length::meter_t(distances[i].get<osrm::json::Number>().value);
//
//    route.emplace_back(Routing::Segment{ s_location, e_location,
//           duration, distance, distance / duration, false });
//  }

  auto steps = legs.at(0).get<osrm::json::Object>().values.at("steps").get<osrm::json::Array>().values;

  bool on_highway = false;

  for (size_t i = 0; i < steps.size(); ++i) {
    auto step = steps[i].get<osrm::json::Object>().values;
    auto coordinates = step.at("geometry").get<osrm::json::Object>().values.at("coordinates").get<osrm::json::Array>().values;
    auto s_lon = coordinates.at(0).get<osrm::json::Array>().values.at(0).get<osrm::json::Number>().value, s_lat = coordinates.at(0).get<osrm::json::Array>().values.at(1).get<osrm::json::Number>().value;
    auto e_lon = coordinates.at(1).get<osrm::json::Array>().values.at(0).get<osrm::json::Number>().value, e_lat = coordinates.at(1).get<osrm::json::Array>().values.at(1).get<osrm::json::Number>().value;
    auto duration = units::time::second_t(step.at("duration").get<osrm::json::Number>().value);
    auto distance = units::length::meter_t(step.at("distance").get<osrm::json::Number>().value);
    auto maneuver_type = step.at("maneuver").get<osrm::json::Object>().values.at("type").get<osrm::json::String>().value;
    if (maneuver_type == "on ramp")
      on_highway = true;
    route.emplace_back(Routing::Segment{ Coordinate{osrm::util::FloatLongitude{s_lon}, osrm::util::FloatLatitude{s_lat}}, Coordinate{osrm::util::FloatLongitude{e_lon}, osrm::util::FloatLatitude{e_lat}}, duration, distance, distance / duration, on_highway });
    if (maneuver_type == "off ramp")
      on_highway = false;
  }

  return route;
}

std::istream &operator>>(std::istream &is, Coordinate &c)
{
  char comma;
  float lat, lon;
  is >> lat >> comma >> lon;
  c = Coordinate{osrm::util::FloatLongitude{lon}, osrm::util::FloatLatitude{lat}};
  return is;
}

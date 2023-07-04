#pragma once

#include "data.hpp"
#include "routing.hpp"

class Hospital {
  friend class Ambulance;
public:
  size_t index;
  enum Type {
    HUB,
    SPOKE,
    FIP,
    PEDIATRIC
  };
  std::string id;
  std::string description;
  Coordinate place;
  Type type;
  static void source(std::istream &is);
protected:
  static std::vector<std::shared_ptr<Hospital>> hospitals;
};

std::ostream &operator<<(std::ostream &os, const Hospital::Type &type);
std::istream &operator>>(std::istream &is, Hospital::Type &type);
std::istream &operator>>(std::istream &is, Hospital &h);


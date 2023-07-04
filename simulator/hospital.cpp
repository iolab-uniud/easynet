#include "data.hpp"
#include "helpers.hpp"

std::istream &operator>>(std::istream &is, Hospital::Type &type) {
  std::string tmp;
  is >> tmp;
  if (tmp == "H")
    type = Hospital::Type::HUB;
  else if (tmp == "S")
    type = Hospital::Type::SPOKE;
  else if (tmp == "PPI")
    type = Hospital::Type::FIP;
  else if (tmp == "K")
    type = Hospital::Type::PEDIATRIC;
  else
    throw std::logic_error("Hospital type (" + tmp + ") not recognized");
  return is;
}

std::ostream& operator<<(std::ostream &os, const Hospital::Type& t) {
  switch (t) {
    case Hospital::Type::HUB:
      os << "HUB";
      break;
    case Hospital::Type::SPOKE:
      os << "SPOKE";
      break;
    case Hospital::Type::FIP:
      os << "FIP";
      break;
    case Hospital::Type::PEDIATRIC:
      os << "PEDIATRIC";
      break;
  }
  return os;
}

std::istream &operator>>(std::istream &is, Hospital &h)
{
  is >> h.id >> h.description >> h.type >> h.place;
  return is;
}

void Hospital::source(std::istream &is)
{
  while (!is.eof())
  {
    std::string tmp;
    std::getline(is, tmp);
    boost::trim(tmp);
    if (tmp == "")
      break;
    auto h = std::make_shared<Hospital>();
    std::istringstream iss(tmp);
    iss >> *h;
    h->index = hospitals.size();
    hospitals.emplace_back(h);
  }
}

std::vector<std::shared_ptr<Hospital>> Hospital::hospitals;

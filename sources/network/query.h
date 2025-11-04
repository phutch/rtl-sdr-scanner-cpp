#pragma once

#include <radio/help_structures.h>
#include <utils/serializers.h>

struct SchedulerQuery {
  std::string latitude;
  std::string longitude;
  int altitude;
  std::string api_key;
  std::vector<Satellite> satellites;
  std::vector<Crontab> crontabs;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SchedulerQuery, latitude, longitude, altitude, api_key, satellites, crontabs)

struct ScheduledTransmission {
  std::string name;
  std::chrono::seconds begin;
  std::chrono::seconds end;
  Frequency frequency;
  Frequency bandwidth;
  std::string modulation;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ScheduledTransmission, name, begin, end, frequency, bandwidth, modulation)

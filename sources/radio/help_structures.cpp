#include "help_structures.h"

Recording::Recording(const Frequency& shift, const bool& flush) : m_shift(shift), m_flush(flush) {}

Satellite::Satellite(const int& id, const std::string& name, const Frequency& frequency, const Frequency& bandwidth, const std::string& modulation)
    : m_id(id), m_name(name), m_frequency(frequency), m_bandwidth(bandwidth), m_modulation(modulation) {}

nlohmann::json Satellite::toJson() const {
  nlohmann::json json;
  json["id"] = m_id;
  json["name"] = m_name;
  json["frequency"] = m_frequency;
  json["bandwidth"] = m_bandwidth;
  json["modulation"] = m_modulation;
  return json;
}
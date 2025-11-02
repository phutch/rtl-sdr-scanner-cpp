#pragma once

#include <notification.h>

#include <algorithm>
#include <complex>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

using Frequency = int32_t;
using FrequencyRange = std::pair<Frequency, Frequency>;

struct Recording {
  Recording(const Frequency& shift, const bool& flush);

  Frequency m_shift;
  bool m_flush;
};

using TransmissionNotification = Notification<std::vector<Recording>>;
using SimpleComplex = std::complex<int8_t>;

struct Satellite {
  Satellite(const int& id, const std::string& name, const Frequency& frequency, const Frequency& bandwidth, const std::string& modulation);
  nlohmann::json toJson() const;

  const int m_id;
  const std::string m_name;
  const Frequency m_frequency;
  const Frequency m_bandwidth;
  const std::string m_modulation;
};

struct Device {
  bool m_enabled{};
  std::vector<std::pair<std::string, float>> m_gains{};
  std::string m_serial{};
  std::string m_driver{};
  Frequency m_sampleRate{};
  std::vector<FrequencyRange> m_ranges{};
  float m_startLevel{};
  float m_stopLevel{};
  std::vector<Satellite> m_satellites{};

  std::string getName() const { return m_driver + "_" + m_serial; }
};

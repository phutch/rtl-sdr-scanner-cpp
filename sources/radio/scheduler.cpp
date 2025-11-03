#include "scheduler.h"

#include <radio/help_structures.h>

#include <chrono>
#include <functional>

constexpr auto LABEL = "scheduler";
constexpr auto LOOP_TIMEOUT = std::chrono::milliseconds(100);
constexpr auto UPDATE_INTERVAL = std::chrono::minutes(60);
constexpr auto SHIFT_FREQUENCY = Frequency(100000);

using namespace std::placeholders;

ScheduledTransmission::ScheduledTransmission(
    const std::string& name, const std::chrono::seconds& begin, const std::chrono::seconds& end, const Frequency& frequency, const Frequency& bandwidth, const std::string& modulation)
    : m_name(name), m_begin(begin), m_end(end), m_frequency(frequency), m_bandwidth(bandwidth), m_modulation(modulation) {}

Scheduler::Scheduler(const Config& config, const Device& device, RemoteController& remoteController)
    : m_config(config), m_device(device), m_remoteController(remoteController), m_lastUpdateTime(0), m_isRefreshEnabled(true), m_isRunning(true), m_thread([this]() { worker(); }) {}

Scheduler::~Scheduler() {
  m_isRunning = false;
  m_thread.join();
}

std::vector<ScheduledTransmission> Scheduler::getTransmissions(const std::chrono::milliseconds& now, std::list<ScheduledTransmission>& scheduledTransmissions) {
  while (!scheduledTransmissions.empty() && scheduledTransmissions.front().m_end < now) {
    scheduledTransmissions.pop_front();
  }
  std::vector<ScheduledTransmission> transmissions;
  for (auto it = scheduledTransmissions.begin(); it != scheduledTransmissions.end() && it->m_begin <= now; ++it) {
    transmissions.push_back(*it);
  }
  return transmissions;
}

std::optional<std::pair<FrequencyRange, std::vector<Recording>>> Scheduler::getRecordings(
    const std::chrono::milliseconds& now, std::list<ScheduledTransmission>& scheduledTransmissions, Frequency sampleRate, Frequency shift) {
  const auto transmissions = getTransmissions(now, scheduledTransmissions);
  if (transmissions.empty()) {
    return std::nullopt;
  }
  const auto center = transmissions.front().m_frequency + shift;
  const auto left = center - sampleRate / 2;
  const auto right = center + sampleRate / 2;
  const FrequencyRange range(left, right);
  std::vector<Recording> recordings;
  for (const auto& transmission : transmissions) {
    const auto transmissionLeft = transmission.m_frequency - transmission.m_bandwidth / 2;
    const auto transmissionRight = transmission.m_frequency + transmission.m_bandwidth / 2;
    if (left <= transmissionLeft && transmissionRight <= right) {
      recordings.emplace_back(transmission.m_frequency - center, true);
    }
  }
  return std::pair<FrequencyRange, std::vector<Recording>>(range, recordings);
}

std::optional<std::pair<FrequencyRange, std::vector<Recording>>> Scheduler::getRecordings(const std::chrono::milliseconds& now) {
  std::unique_lock lock(m_mutex);
  return Scheduler::getRecordings(now, m_scheduledTransmissions, m_device.m_sampleRate, SHIFT_FREQUENCY);
}

void Scheduler::worker() {
  m_remoteController.satellitesCallback(m_device.getName(), std::bind(&Scheduler::satellitesCallback, this, _1));
  while (m_isRunning) {
    const auto now = getTime();
    if (m_isRefreshEnabled && m_lastUpdateTime + UPDATE_INTERVAL <= now) {
      satellitesQuery();
      m_lastUpdateTime = now;
    }
    std::this_thread::sleep_for(LOOP_TIMEOUT);
  }
}

void Scheduler::satellitesQuery() {
  Logger::info(LABEL, "send satellites query");
  nlohmann::json satellites = nlohmann::json::array();
  for (const auto& satellite : m_device.m_satellites) {
    satellites.push_back(satellite.toJson());
  }

  nlohmann::json json;
  json["satellites"] = satellites;
  json["latitude"] = m_config.latitude();
  json["longitude"] = m_config.longitude();
  json["altitude"] = m_config.altitude();
  json["api_key"] = m_config.apiKey();
  m_remoteController.satellitesQuery(m_device.getName(), json.dump());
}

void Scheduler::satellitesCallback(const nlohmann::json& json) {
  Logger::info(LABEL, "received satellites: {}", colored(GREEN, "{}", json.dump()));

  std::list<ScheduledTransmission> scheduledTransmissions;
  for (const auto& item : json) {
    const auto name = item.at("name").get<std::string>();
    const auto begin = std::chrono::seconds(item.at("begin").get<uint64_t>());
    const auto end = std::chrono::seconds(item.at("end").get<uint64_t>());
    const auto frequency = item.at("frequency").get<Frequency>();
    const auto bandwidth = item.at("bandwidth").get<Frequency>();
    const auto modulation = item.at("modulation").get<std::string>();
    scheduledTransmissions.emplace_back(name, begin, end, frequency, bandwidth, modulation);
  }

  std::unique_lock lock(m_mutex);
  m_scheduledTransmissions = scheduledTransmissions;
}

void Scheduler::setRefreshEnabled(const bool& enabled) { m_isRefreshEnabled = enabled; }
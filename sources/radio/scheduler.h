#pragma once

#include <config.h>
#include <network/remote_controller.h>
#include <radio/sdr_device.h>

#include <list>
#include <mutex>
#include <optional>

struct ScheduledTransmission {
  ScheduledTransmission(
      const std::string& m_name, const std::chrono::seconds& m_begin, const std::chrono::seconds& m_end, const Frequency& m_frequency, const Frequency& m_bandwidth, const std::string& m_modulation);

  std::string m_name;
  std::chrono::seconds m_begin;
  std::chrono::seconds m_end;
  Frequency m_frequency;
  Frequency m_bandwidth;
  std::string m_modulation;
};

class Scheduler {
 public:
  Scheduler(const Config& config, const Device& device, RemoteController& remoteController);
  ~Scheduler();

  static std::vector<ScheduledTransmission> getTransmissions(const std::chrono::milliseconds& now, std::list<ScheduledTransmission>& scheduledTransmissions);
  static std::optional<std::pair<FrequencyRange, std::vector<Recording>>> getRecordings(
      const std::chrono::milliseconds& now, std::list<ScheduledTransmission>& scheduledTransmissions, Frequency sampleRate, Frequency shift);
  std::optional<std::pair<FrequencyRange, std::vector<Recording>>> getRecordings(const std::chrono::milliseconds& now);

  void worker();
  void satellitesQuery();
  void satellitesCallback(const nlohmann::json& json);
  void setRefreshEnabled(const bool& enabled);

  const Config& m_config;
  const Device m_device;
  RemoteController& m_remoteController;
  std::list<ScheduledTransmission> m_scheduledTransmissions;
  std::chrono::milliseconds m_lastUpdateTime;
  std::atomic<bool> m_isRefreshEnabled;
  std::atomic<bool> m_isRunning;
  std::thread m_thread;
  std::mutex m_mutex;
};

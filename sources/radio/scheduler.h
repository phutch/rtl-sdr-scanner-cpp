#pragma once

#include <config.h>
#include <network/remote_controller.h>
#include <radio/sdr_device.h>

class Scheduler {
 public:
  Scheduler(const Config& config, const Device& device, RemoteController& remoteController);
  ~Scheduler();

 private:
  void worker();
  void satellitesQuery();
  void satellitesCallback(const nlohmann::json& json);

  const Config& m_config;
  const Device m_device;
  RemoteController& m_remoteController;
  std::chrono::milliseconds m_lastUpdateTime;
  std::atomic<bool> m_isRunning;
  std::thread m_thread;
};

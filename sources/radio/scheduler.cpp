#include "scheduler.h"

#include <radio/help_structures.h>

#include <chrono>
#include <functional>

constexpr auto LABEL = "scheduler";
constexpr auto LOOP_TIMEOUT = std::chrono::milliseconds(100);
constexpr auto UPDATE_INTERVAL = std::chrono::minutes(60);

using namespace std::placeholders;

Scheduler::Scheduler(const Config& config, const Device& device, RemoteController& remoteController)
    : m_config(config), m_device(device), m_remoteController(remoteController), m_lastUpdateTime(0), m_isRunning(true), m_thread([this]() { worker(); }) {}

Scheduler::~Scheduler() {
  m_isRunning = false;
  m_thread.join();
}

void Scheduler::worker() {
  m_remoteController.satellitesCallback(m_device.getName(), std::bind(&Scheduler::satellitesCallback, this, _1));
  while (m_isRunning) {
    const auto now = getTime();
    if (m_lastUpdateTime + UPDATE_INTERVAL <= now) {
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

void Scheduler::satellitesCallback(const nlohmann::json& json) { Logger::info(LABEL, "received satellites: {}", colored(GREEN, "{}", json.dump())); }

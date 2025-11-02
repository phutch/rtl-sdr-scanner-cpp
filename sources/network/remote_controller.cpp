#include "remote_controller.h"

#include <utils/utils.h>

#include <nlohmann/json.hpp>

constexpr auto LIST = "list";
constexpr auto STATUS = "status";
constexpr auto CONFIG = "config";
constexpr auto SUCCESS = "success";
constexpr auto FAILED = "failed";
constexpr auto LABEL = "remote";

using namespace std::placeholders;

RemoteController::RemoteController(const Config& config, Mqtt& mqtt) : m_config(config), m_mqtt(mqtt) {
  mqtt.setRawMessageCallback(fmt::format("sdr/{}", LIST), std::bind(&RemoteController::listCallback, this, _1));
  Logger::info(LABEL, "started, id: {}", colored(GREEN, "{}", m_config.getId()));
}

void RemoteController::reloadConfigCallback(const Mqtt::JsonCallback& callback) { m_mqtt.setJsonMessageCallback(fmt::format("sdr/{}/{}", CONFIG, m_config.getId()), callback); }

void RemoteController::reloadConfigStatus(const bool& success) { m_mqtt.publish(fmt::format("sdr/{}/{}/{}", CONFIG, m_config.getId(), success ? SUCCESS : FAILED), "", 2); }

void RemoteController::listCallback(const std::string&) {
  Logger::info(LABEL, "received list");
  m_mqtt.publish(fmt::format("sdr/{}/{}", STATUS, m_config.getId()), m_config.json().dump(), 2);
}

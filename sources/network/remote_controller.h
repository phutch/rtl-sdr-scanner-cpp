#pragma once

#include <config.h>
#include <network/mqtt.h>

#include <functional>
#include <nlohmann/json.hpp>

class RemoteController {
 public:
  RemoteController(const Config& config, Mqtt& mqtt);

  void reloadConfigCallback(const Mqtt::JsonCallback& callback);
  void reloadConfigStatus(const bool& success);

 private:
  void listCallback(const std::string& data);

  const Config& m_config;
  Mqtt& m_mqtt;
};

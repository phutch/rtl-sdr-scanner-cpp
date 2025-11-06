#include "file_config.h"

#include <config_migrator.h>
#include <logger.h>
#include <radio/sdr_device_reader.h>

#include <regex>

constexpr auto LABEL = "config";

FileConfig FileConfig::fromJson(nlohmann::json json) {
  ConfigMigrator::update(json);
  FileConfig fileConfig(json);
  SdrDeviceReader::updateDevices(fileConfig.devices);
  return fileConfig;
}

nlohmann::json FileConfig::toSave(nlohmann::json json) {
  SdrDeviceReader::clearDevices(json);
  ConfigMigrator::sort(json);
  return json;
}

nlohmann::json FileConfig::toPrint(nlohmann::json json) {
  try {
    if (!json["api_key"].empty()) {
      json["api_key"] = "******";
    }
    std::regex regex(R"(^(\d+)\.\d+)");
    json["position"]["latitude"] = std::regex_replace(json["position"]["latitude"].get<std::string>(), regex, "$1.********");
    json["position"]["longitude"] = std::regex_replace(json["position"]["longitude"].get<std::string>(), regex, "$1.********");
  } catch (const std::exception& exception) {
    Logger::warn(LABEL, "hide sensitive data exception: {}", colored(RED, "{}", exception.what()));
  }
  return json;
}
#include <SoapySDR/Logger.h>
#include <config.h>
#include <logger.h>
#include <network/mqtt.h>
#include <network/remote_controller.h>
#include <scanner.h>
#include <signal.h>
#include <utils/file_utils.h>

#include <CLI/CLI.hpp>
#include <memory>
#include <thread>

constexpr auto LABEL = "main";

volatile bool isRunning{true};

void handler(int) {
  Logger::warn(LABEL, "{}", colored(RED, "{}", "received stop signal"));
  isRunning = false;
}

int main(int argc, char** argv) {
  CLI::App app("sdr-scanner");
  argv = app.ensure_utf8(argv);

  ArgConfig argConfig;
  app.add_option("--config", argConfig.configFile, "config file")->required();
  app.add_option("--id", argConfig.id);
  app.add_option("--log-file", argConfig.logFileName, "log file");
  app.add_option("--log-file-count", argConfig.logFileCount, "log file count")->check(CLI::PositiveNumber);
  app.add_option("--log-file-size", argConfig.logFileSize, "log file size")->check(CLI::PositiveNumber);
  app.add_option("--mqtt-url", argConfig.mqttUrl, "mqtt url")->required();
  app.add_option("--mqtt-user", argConfig.mqttUser, "mqtt username")->required();
  app.add_option("--mqtt-password", argConfig.mqttPassword, "mqtt password")->required();
  CLI11_PARSE(app, argc, argv);

  dup2(fileno(fopen("/dev/null", "w")), fileno(stderr));
  SoapySDR_setLogLevel(SoapySDRLogLevel::SOAPY_SDR_WARNING);
  signal(SIGINT, handler);
  signal(SIGTERM, handler);

  try {
    Logger::configure(spdlog::level::info, spdlog::level::info, argConfig.logFileName, argConfig.logFileSize, argConfig.logFileCount, true);
    Logger::info(LABEL, "{}", colored(GREEN, "{}", "starting"));

    nlohmann::json tmpJson;
    while (isRunning) {
      bool reload = false;
      const auto fileJson = tmpJson.empty() ? readFromFile(argConfig.configFile, static_cast<nlohmann::json>(FileConfig())) : tmpJson;
      const auto fileConfig = FileConfig::fromJson(fileJson);
      const Config config(argConfig, fileConfig);
      Logger::configure(config.consoleLogLevel(), config.fileLogLevel(), argConfig.logFileName, argConfig.logFileSize, argConfig.logFileCount, config.isColorLogEnabled());
      Logger::info(LABEL, "config: {}", colored(GREEN, "{}", FileConfig::toPrint(fileJson).dump()));
      Logger::info(LABEL, "mqtt: {}", colored(GREEN, "{}", config.mqtt()));

      Mqtt mqtt(config);
      RemoteController remoteController(config, mqtt);
      remoteController.setConfigQuery([&reload, &argConfig, &remoteController, &tmpJson](const nlohmann::json& json) {
        try {
          Logger::info(LABEL, "set config: {}", colored(GREEN, "{}", FileConfig::toPrint(json).dump()));
          saveToFile(argConfig.configFile, FileConfig::toSave(json));
          reload = true;
          tmpJson.clear();
          remoteController.setConfigResponse(true);
        } catch (const std::runtime_error& exception) {
          Logger::warn(LABEL, "set config exception: {}", exception.what());
          remoteController.setConfigResponse(false);
        }
      });
      remoteController.resetTmpConfigQuery([&reload, &argConfig, &remoteController, &tmpJson](const std::string&) {
        Logger::info(LABEL, "reset tmp config");
        reload = true;
        tmpJson.clear();
        remoteController.resetTmpConfigResponse(true);
      });
      remoteController.setTmpConfigQuery([&reload, &argConfig, &remoteController, &tmpJson](const nlohmann::json& json) {
        try {
          Logger::info(LABEL, "set tmp config: {}", colored(GREEN, "{}", FileConfig::toPrint(json).dump()));
          reload = true;
          tmpJson = json;
          remoteController.setConfigResponse(true);
        } catch (const std::runtime_error& exception) {
          Logger::warn(LABEL, "set tmp config exception: {}", exception.what());
          remoteController.setConfigResponse(false);
        }
      });
      remoteController.getConfigQuery([&remoteController, &fileConfig](const std::string&) {
        Logger::info(LABEL, "get config");
        remoteController.getConfigResponse(static_cast<nlohmann::json>(fileConfig).dump());
      });
      std::vector<std::unique_ptr<Scanner>> scanners;
      for (const auto& device : config.devices()) {
        try {
          if (!device.enabled) {
            Logger::info(LABEL, "device disabled, skipping: {}", colored(GREEN, "{}", device.getName()));
          } else {
            scanners.push_back(std::make_unique<Scanner>(config, device, mqtt, remoteController, config.recordersCount()));
          }
        } catch (const std::exception& exception) {
          Logger::error(LABEL, "can not open device: {}, exception: {}", colored(RED, "{}", device.getName()), exception.what());
        }
      }
      if (scanners.empty()) {
        Logger::warn(LABEL, "{}", colored(RED, "{}", "empty devices list"));
      }

      Logger::info(LABEL, "{}", colored(GREEN, "{}", "started"));
      while (isRunning && !reload) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
    Logger::info(LABEL, "{}", colored(GREEN, "{}", "stopped"));
  } catch (const std::exception& exception) {
    Logger::error(LABEL, "exception: {}", exception.what());
  }
  return 0;
}

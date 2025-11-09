#include "sdr_device_reader.h"

#include <config.h>
#include <logger.h>
#include <utils/utils.h>

constexpr auto LABEL = "sdr_reader";

std::vector<Frequency> getSampleRates(SoapySDR::Device* sdr) {
  std::vector<Frequency> sampleRates;
  for (const auto value : sdr->listSampleRates(SOAPY_SDR_RX, 0)) {
    const auto sampleRate = static_cast<Frequency>(value);
    Logger::info(LABEL, "  supported sample rate: {}", formatFrequency(sampleRate));
    sampleRates.emplace_back(sampleRate);
  }
  std::sort(sampleRates.begin(), sampleRates.end());
  return sampleRates;
}

std::vector<Gain> getGains(SoapySDR::Device* sdr) {
  std::vector<Gain> gains;
  for (const auto& gain : sdr->listGains(SOAPY_SDR_RX, 0)) {
    const auto gainRange = sdr->getGainRange(SOAPY_SDR_RX, 0, gain);
    Logger::info(
        LABEL,
        "  supported gain: {}, min: {}, max: {}, step: {}",
        colored(GREEN, "{}", gain),
        colored(GREEN, "{}", gainRange.minimum()),
        colored(GREEN, "{}", gainRange.maximum()),
        colored(GREEN, "{}", gainRange.step()));
    std::vector<double> options;
    const auto optionsCount = (gainRange.maximum() - gainRange.minimum()) / gainRange.step();
    if (!std::isnan(optionsCount) && optionsCount < 1000) {
      for (double value = gainRange.minimum(); value <= gainRange.maximum(); value += gainRange.step()) {
        options.push_back(value);
      }
    } else if (sdr->getDriverKey() == "RTLSDR" && gain == "TUNER") {
      options = {0.0, 0.9, 1.4, 2.7, 3.7, 7.7, 8.7, 12.5, 14.4, 15.7, 16.6, 19.7, 20.7, 22.9, 25.4, 28.0, 29.7, 32.8, 33.8, 36.4, 37.2, 38.6, 40.2, 42.1, 43.4, 43.9, 44.5, 48.0, 49.6};
    } else {
      options.push_back(gainRange.minimum());
      options.push_back(gainRange.maximum());
    }
    gains.emplace_back(gain, gainRange.maximum(), gainRange.minimum(), gainRange.maximum(), gainRange.step(), options);
  }
  std::sort(gains.begin(), gains.end(), [](const Gain& g1, Gain& g2) { return g1.name < g2.name; });
  return gains;
}

void SdrDeviceReader::updateDevice(Device& device, const SoapySDR::Kwargs args) {
  const auto serial = args.at("serial");
  const auto driver = args.at("driver");
  Logger::info(LABEL, "update device, driver: {}, serial: {}", colored(GREEN, "{}", driver), colored(GREEN, "{}", serial));

  SoapySDR::Device* sdr = SoapySDR::Device::make(args);
  if (sdr == nullptr) {
    throw std::runtime_error("open device failed");
  }

  device.connected = true;
  device.driver = driver;
  device.sample_rates = getSampleRates(sdr);
  const auto backupGains = device.gains;
  device.gains = getGains(sdr);
  for (auto& gain : device.gains) {
    for (auto& backupGain : backupGains) {
      if (gain.name == backupGain.name) {
        gain.value = backupGain.value;
      }
    }
  }

  SoapySDR::Device::unmake(sdr);
}

Device SdrDeviceReader::createDevice(const SoapySDR::Kwargs args) {
  const auto serial = args.at("serial");
  const auto driver = args.at("driver");
  Logger::info(LABEL, "creating device, driver: {}, serial: {}", colored(GREEN, "{}", driver), colored(GREEN, "{}", serial));

  SoapySDR::Device* sdr = SoapySDR::Device::make(args);
  if (sdr == nullptr) {
    throw std::runtime_error("open device failed");
  }

  Device device;
  device.connected = true;
  device.driver = driver;
  device.serial = serial;
  device.enabled = true;
  device.start_recording_level = DEFAULT_RECORDING_START_LEVEL;
  device.stop_recording_level = DEFAULT_RECORDING_STOP_LEVEL;
  device.sample_rates = getSampleRates(sdr);
  device.gains = getGains(sdr);

  auto addSampleRate = [&device](Frequency start, Frequency stop, Frequency sampleRate) {
    const auto contains = std::find(device.sample_rates.begin(), device.sample_rates.end(), sampleRate) != device.sample_rates.end();
    if (device.ranges.empty() && contains) {
      device.ranges.emplace_back(start, stop);
      device.sample_rate = sampleRate;
    }
  };

  addSampleRate(140000000, 160000000, 20480000);
  addSampleRate(140000000, 160000000, 20000000);
  addSampleRate(144000000, 146000000, 2048000);
  addSampleRate(144000000, 146000000, 2000000);
  addSampleRate(144000000, 146000000, 1024000);
  addSampleRate(144000000, 146000000, 1000000);
  addSampleRate(144000000, 146000000, *device.sample_rates.rbegin());

  SoapySDR::Device::unmake(sdr);
  return device;
}

void SdrDeviceReader::updateDevices(std::vector<Device>& devices) {
  Logger::info(LABEL, "scanning connected devices");
  const SoapySDR::KwargsList results = SoapySDR::Device::enumerate("remote=");
  Logger::info(LABEL, "found {} devices:", colored(GREEN, "{}", results.size()));

  for (uint32_t i = 0; i < results.size(); ++i) {
    try {
      const auto serial = results[i].at("serial");
      const auto f = [serial](const Device& device) { return device.serial == serial; };
      const auto it = std::find_if(devices.begin(), devices.end(), f);
      if (it != devices.end()) {
        updateDevice(*it, results[i]);
      } else {
        devices.push_back(createDevice(results[i]));
      }
    } catch (const std::exception& exception) {
      Logger::exception(LABEL, exception, SPDLOG_LOC, "update device failed");
    }
  }
}

void SdrDeviceReader::clearDevices(nlohmann::json& json) {
  for (auto& device : json.at("devices")) {
    device.erase("connected");
    device.erase("driver");
    device.erase("sample_rates");

    for (auto& gain : device.at("gains")) {
      gain.erase("min");
      gain.erase("max");
      gain.erase("step");
      gain.erase("options");
    }
  }
}

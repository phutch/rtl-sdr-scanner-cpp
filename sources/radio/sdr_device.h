#pragma once

#include <gnuradio/top_block.h>
#include <network/remote_controller.h>
#include <notification.h>
#include <radio/blocks/sdr_source.h>
#include <radio/help_structures.h>
#include <radio/recorder.h>
#include <radio/sdr_processor.h>

#include <memory>
#include <set>
#include <string>

class SdrDevice {
 public:
  SdrDevice(const Config& config, const Device& device, RemoteController& remoteController, TransmissionNotification& notification);
  ~SdrDevice();

  void setFrequencyRange(FrequencyRange frequencyRange);
  void updateRecordings(const std::vector<Recording> recordings);

 private:
  const Config& m_config;
  const Device m_device;
  const std::string m_zeromq;
  RemoteController& m_remoteController;
  TransmissionNotification& m_notification;
  bool m_isInitialized;

  std::shared_ptr<gr::top_block> m_tb;
  Connector m_connector;
  std::shared_ptr<SdrSource> m_source;
  std::unique_ptr<SdrProcessor> m_processor;
  std::vector<std::unique_ptr<Recorder>> m_recorders;
  std::set<Frequency> ignoredTransmissions;
};

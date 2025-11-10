#pragma once

#include <config.h>
#include <gnuradio/top_block.h>
#include <network/remote_controller.h>
#include <radio/connector.h>
#include <radio/help_structures.h>

#include <memory>

class SdrProcessor {
 public:
  SdrProcessor(const Config& config, const Device& device, RemoteController& remoteController, TransmissionNotification& notification, const std::string& zeromq, const FrequencyRange& frequencyRange);
  ~SdrProcessor();

 private:
  std::shared_ptr<gr::top_block> m_tb;
  Connector m_connector;
};
#include "sdr_device.h"

#include <config.h>
#include <gnuradio/block_detail.h>
#include <gnuradio/blocks/float_to_char.h>
#include <gnuradio/blocks/stream_to_vector.h>
#include <gnuradio/fft/fft_v.h>
#include <gnuradio/fft/window.h>
#include <gnuradio/soapy/source.h>
#include <gnuradio/zeromq/pub_sink.h>
#include <logger.h>
#include <radio/blocks/decimator.h>
#include <radio/blocks/psd.h>
#include <radio/blocks/spectrogram.h>

#include <filesystem>

constexpr auto LABEL = "sdr";

SdrDevice::SdrDevice(const Config& config, const Device& device, RemoteController& remoteController, TransmissionNotification& notification)
    : m_config(config),
      m_device(device),
      m_zeromq(fmt::format("ipc://{}/{}_{}_zeromq_stream.sock", std::filesystem::canonical(m_config.workDir()).string(), device.driver, device.serial)),
      m_remoteController(remoteController),
      m_sampleRate(device.sample_rate),
      m_isInitialized(false),
      m_frequencyRange({0, 0}),
      m_tb(gr::make_top_block("sdr")),
      m_powerFileSink(nullptr),
      m_rawIqFileSink(nullptr),
      m_connector(m_tb) {
  Logger::info(LABEL, "starting");
  Logger::info(LABEL, "driver: {}, serial: {}, sample rate: {}", colored(GREEN, "{}", device.driver), colored(GREEN, "{}", device.serial), formatFrequency(m_sampleRate));
  Logger::info(LABEL, "zeromq: {}", colored(GREEN, "{}", m_zeromq));

  m_source = std::make_shared<SdrSource>(device);
  setupChains(config, device, remoteController, notification);

  m_tb->start();
  Logger::info(LABEL, "started");
}

SdrDevice::~SdrDevice() {
  Logger::info(LABEL, "stopping");
  m_tb->stop();
  m_tb->wait();
  std::remove(m_zeromq.c_str());
  Logger::info(LABEL, "stopped");
}

void SdrDevice::setFrequencyRange(FrequencyRange frequencyRange) {
  if (!m_isInitialized) {
    Logger::info(LABEL, "waiting, initial sleep: {}", colored(GREEN, "{} ms", INITIAL_DELAY.count()));
    std::this_thread::sleep_for(INITIAL_DELAY);
    Logger::info(LABEL, "finished, initial sleep");
    m_isInitialized = true;
    m_blocker->setBlocking(false);
  }

  m_blocker->setBlocking(true);
  if (m_powerFileSink) m_powerFileSink->stopRecording();
  if (m_rawIqFileSink) m_rawIqFileSink->stopRecording();

  const auto frequency = frequencyRange.center();
  if (m_source->setCenterFrequency(frequency)) {
    Logger::debug(LABEL, "set frequency range: {}, center frequency: {}", formatFrequencyRange(frequencyRange), formatFrequency(frequency));
  } else {
    Logger::warn(LABEL, "set frequency range failed: {}, center frequency: {}", formatFrequencyRange(frequencyRange), formatFrequency(frequency));
  }

  m_transmission->resetBuffers();
  if (m_powerFileSink) m_powerFileSink->startRecording(getRawFileName("full", "power", frequency, m_sampleRate));
  if (m_rawIqFileSink) m_rawIqFileSink->startRecording(getRawFileName("full", "fc", frequency, m_sampleRate));
  m_frequencyRange = frequencyRange;
  m_blocker->skip();
  m_blocker->setBlocking(false);
}

void SdrDevice::updateRecordings(const std::vector<Recording> recordings) {
  const auto findRecorder = [this](const Recording& recording) {
    return std::find_if(m_recorders.begin(), m_recorders.end(), [recording](const std::unique_ptr<Recorder>& recorder) {
      // improve auto formatter
      return recording.recordingFrequency == recorder->getRecording().recordingFrequency;
    });
  };

  const auto isRecordingActive = [recordings](const Recording& recording1) {
    return std::find_if(recordings.begin(), recordings.end(), [recording1](const Recording& recording2) {
             // improve auto formatter
             return recording1.recordingFrequency == recording2.recordingFrequency;
           }) != recordings.end();
  };

  std::erase_if(m_recorders, [isRecordingActive](const std::unique_ptr<Recorder>& recorder) { return !isRecordingActive(recorder->getRecording()); });

  if (m_recorders.size() < static_cast<size_t>(m_config.recordersCount())) {
    ignoredTransmissions.clear();
  }

  for (const auto& recording : recordings) {
    const auto it = findRecorder(recording);
    if (it != m_recorders.end()) {
      if (recording.flush) {
        (*it)->flush();
      }
    } else {
      if (m_recorders.size() < static_cast<size_t>(m_config.recordersCount())) {
        m_recorders.push_back(
            std::make_unique<Recorder>(m_config, m_zeromq, m_sampleRate, recording, std::bind(&RemoteController::sendTransmission, m_remoteController, m_device.getName(), std::placeholders::_1)));
      } else {
        if (!ignoredTransmissions.count(recording.recordingFrequency)) {
          Logger::info(LABEL, "maximum recorders limit reached, frequency: {}", formatFrequency(recording.recordingFrequency, RED));
          ignoredTransmissions.insert(recording.recordingFrequency);
        }
      }
    }
  }
}

Frequency SdrDevice::getFrequency() const { return m_frequencyRange.center(); }

void SdrDevice::setupChains(const Config& config, const Device& device, RemoteController& remoteController, TransmissionNotification& notification) {
  const auto fftSize = getFft(m_sampleRate, SIGNAL_DETECTION_MAX_STEP);
  const auto step = static_cast<double>(m_sampleRate) / fftSize;
  const auto indexStep = static_cast<Frequency>(std::ceil(config.recordingBandwidth() / (static_cast<double>(m_sampleRate) / fftSize)));
  const auto decimatorFactor = std::max(1, static_cast<int>(step / SIGNAL_DETECTION_FPS));
  const auto indexToFrequency = [this, step](const int index) { return getFrequency() + static_cast<Frequency>(step * (index + 0.5)) - m_sampleRate / 2; };
  const auto indexToShift = [this, step](const int index) { return static_cast<Frequency>(step * (index + 0.5)) - m_sampleRate / 2; };
  const auto isIndexInRange = [this, indexToFrequency](const int index) { return m_frequencyRange.contains(indexToFrequency(index)); };
  Logger::info(LABEL, "signal detection, fft: {}, step: {}, decimator factor: {}", colored(GREEN, "{}", fftSize), formatFrequency(step), colored(GREEN, "{}", decimatorFactor));

  const auto s2c = gr::blocks::stream_to_vector::make(sizeof(gr_complex), fftSize * decimatorFactor);
  m_blocker = std::make_shared<Blocker>(gr::io_signature::make(1, 1, sizeof(gr_complex) * fftSize * decimatorFactor), true);
  const auto decimator = std::make_shared<Decimator<gr_complex>>(fftSize, decimatorFactor);
  const auto fft = gr::fft::fft_v<gr_complex, true>::make(fftSize, gr::fft::window::hamming(fftSize), true);
  const auto psd = std::make_shared<PSD>(fftSize, m_sampleRate);
  m_noiseLearner = std::make_shared<NoiseLearner>(fftSize, std::bind(&SdrDevice::getFrequency, this), indexToFrequency);
  m_transmission = std::make_shared<Transmission>(config, device, fftSize, indexStep, notification, std::bind(&SdrDevice::getFrequency, this), indexToFrequency, indexToShift, isIndexInRange);
  m_connector.connect<Block>(m_source, s2c, m_blocker, decimator, fft, psd, m_noiseLearner, m_transmission);

  const auto spectrogram = std::make_shared<Spectrogram>(
      fftSize, m_sampleRate, std::bind(&SdrDevice::getFrequency, this), std::bind(&RemoteController::sendSpectrogram, remoteController, device.getName(), std::placeholders::_1));
  m_connector.connect<Block>(psd, spectrogram);

  if (DEBUG_SAVE_FULL_POWER) {
    m_powerFileSink = std::make_shared<FileSink<float>>(fftSize, false);
    m_connector.connect<Block>(psd, m_powerFileSink);
  }

  if (DEBUG_SAVE_FULL_RAW_IQ) {
    m_rawIqFileSink = std::make_shared<FileSink<gr_complex>>(1, false);
    m_connector.connect<Block>(m_source, m_rawIqFileSink);
  }

  auto sink = gr::zeromq::pub_sink::make(sizeof(gr_complex), 1, const_cast<char*>(m_zeromq.c_str()));
  m_connector.connect<Block>(m_source, sink);
}

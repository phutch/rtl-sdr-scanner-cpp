#include "sdr_processor.h"

#include <gnuradio/blocks/file_sink.h>
#include <gnuradio/blocks/float_to_char.h>
#include <gnuradio/blocks/stream_to_vector.h>
#include <gnuradio/fft/fft_v.h>
#include <gnuradio/fft/window.h>
#include <gnuradio/zeromq/sub_source.h>
#include <radio/blocks/decimator.h>
#include <radio/blocks/noise_learner.h>
#include <radio/blocks/psd.h>
#include <radio/blocks/spectrogram.h>
#include <radio/blocks/transmission.h>
#include <utils/radio_utils.h>

constexpr auto LABEL = "processor";

SdrProcessor::SdrProcessor(
    const Config& config, const Device& device, RemoteController& remoteController, TransmissionNotification& notification, const std::string& zeromq, const FrequencyRange& frequencyRange)
    : m_tb(gr::make_top_block("processor")), m_connector(m_tb) {
  const auto getFrequency = [frequencyRange]() { return frequencyRange.center(); };
  const auto sampleRate = device.sample_rate;

  const auto fftSize = getFft(sampleRate, SIGNAL_DETECTION_MAX_STEP);
  const auto step = static_cast<double>(sampleRate) / fftSize;
  const auto indexStep = static_cast<Frequency>(std::ceil(config.recordingBandwidth() / (static_cast<double>(sampleRate) / fftSize)));
  const auto decimatorFactor = std::max(1, static_cast<int>(step / SIGNAL_DETECTION_FPS));
  const auto indexToFrequency = [sampleRate, frequencyRange, step](const int index) { return frequencyRange.center() + static_cast<Frequency>(step * (index + 0.5)) - sampleRate / 2; };
  const auto indexToShift = [sampleRate, step](const int index) { return static_cast<Frequency>(step * (index + 0.5)) - sampleRate / 2; };
  const auto isIndexInRange = [frequencyRange, indexToFrequency](const int index) { return frequencyRange.contains(indexToFrequency(index)); };
  Logger::info(LABEL, "signal detection, fft: {}, step: {}, decimator factor: {}", colored(GREEN, "{}", fftSize), formatFrequency(step), colored(GREEN, "{}", decimatorFactor));

  const auto source = gr::zeromq::sub_source::make(sizeof(gr_complex), 1, const_cast<char*>(zeromq.c_str()));
  const auto s2c = gr::blocks::stream_to_vector::make(sizeof(gr_complex), fftSize * decimatorFactor);
  const auto decimator = std::make_shared<Decimator<gr_complex>>(fftSize, decimatorFactor);
  const auto fft = gr::fft::fft_v<gr_complex, true>::make(fftSize, gr::fft::window::hamming(fftSize), true);
  const auto psd = std::make_shared<PSD>(fftSize, sampleRate);
  const auto noiseLearner = std::make_shared<NoiseLearner>(fftSize, getFrequency, indexToFrequency);
  const auto transmission = std::make_shared<Transmission>(config, device, fftSize, indexStep, notification, getFrequency, indexToFrequency, indexToShift, isIndexInRange);
  m_connector.connect<Block>(source, s2c, decimator, fft, psd, noiseLearner, transmission);

  const auto spectrogram = std::make_shared<Spectrogram>(fftSize, sampleRate, getFrequency, std::bind(&RemoteController::sendSpectrogram, remoteController, device.getName(), std::placeholders::_1));
  m_connector.connect<Block>(psd, spectrogram);

  if (DEBUG_SAVE_FULL_POWER) {
    const auto fileName = getRawFileName("full", "power", frequencyRange.center(), device.sample_rate);
    m_connector.connect<Block>(psd, gr::blocks::file_sink::make(sizeof(float) * fftSize, fileName.c_str()));
  }

  if (DEBUG_SAVE_FULL_RAW_IQ) {
    const auto fileName = getRawFileName("full", "fc", frequencyRange.center(), device.sample_rate);
    m_connector.connect<Block>(source, gr::blocks::file_sink::make(sizeof(gr_complex), fileName.c_str()));
  }

  m_tb->start();
  Logger::info(LABEL, "started");
}

SdrProcessor::~SdrProcessor() {
  m_tb->stop();
  m_tb->wait();
  Logger::info(LABEL, "stopped");
}
#include "recorder.h"

#include <config.h>
#include <gnuradio/blocks/complex_to_interleaved_char.h>
#include <gnuradio/blocks/stream_to_vector.h>
#include <gnuradio/filter/rational_resampler.h>
#include <gnuradio/zeromq/sub_source.h>
#include <logger.h>
#include <network/query.h>
#include <radio/blocks/file_sink.h>

#include <limits>

constexpr auto LABEL = "recorder";

Recorder::Recorder(const Config& config, const std::string& zeromq, Frequency sampleRate, const Recording& recording, std::function<void(const nlohmann::json&)> send)
    : m_config(config), m_sampleRate(sampleRate), m_recording(recording), m_send(send), m_tb(gr::make_top_block("recorder")), m_rawFileSinkBlock(nullptr), m_connector(m_tb) {
  Logger::info(
      LABEL,
      "start recorder, source: {}, name: {}, frequency: {}, bandwidth: {}, modulation: {}",
      colored(BLUE, "{}", m_recording.source),
      colored(BLUE, "{}", m_recording.name),
      formatFrequency(m_recording.recordingFrequency, GREEN),
      formatFrequency(m_recording.bandwidth, GREEN),
      colored(BLUE, "{}", m_recording.modulation));

  auto source = gr::zeromq::sub_source::make(sizeof(gr_complex), 1, const_cast<char*>(zeromq.c_str()));
  std::vector<Block> blocks;
  m_shiftBlock = gr::blocks::rotator_cc::make();
  blocks.push_back(source);
  blocks.push_back(m_shiftBlock);

  Block lastResampler;
  for (const auto& [factor1, factor2] : getResamplersFactors(m_sampleRate, m_recording.bandwidth, RESAMPLER_THRESHOLD)) {
    Logger::debug(LABEL, "rational resampler factors: {}, {}", colored(GREEN, "{}", factor1), colored(GREEN, "{}", factor2));
    lastResampler = gr::filter::rational_resampler<gr_complex, gr_complex, gr_complex>::make(factor1, factor2);
    blocks.push_back(lastResampler);
  }

  const auto samplesSize = roundUp(m_recording.bandwidth * RECORDER_FLUSH_INTERVAL.count() / 1000, 4096);
  blocks.push_back(gr::blocks::complex_to_interleaved_char::make(true, 127.0));
  blocks.push_back(gr::blocks::stream_to_vector::make(sizeof(SimpleComplex), samplesSize));
  m_buffer = std::make_shared<Buffer<SimpleComplex>>("RecorderBuffer", samplesSize);
  blocks.push_back(m_buffer);
  m_connector.connect(blocks);

  if (DEBUG_SAVE_RECORDING_RAW_IQ) {
    m_rawFileSinkBlock = std::make_shared<FileSink<gr_complex>>(1, true);
    m_connector.connect(lastResampler, m_rawFileSinkBlock);
  }

  m_firstDataTime = getTime();
  m_lastDataTime = m_firstDataTime;
  m_shiftBlock->set_phase_inc(2.0l * M_PIl * (static_cast<double>(-m_recording.shift()) / static_cast<float>(m_sampleRate)));
  if (m_rawFileSinkBlock) {
    m_rawFileSinkBlock->startRecording(getRawFileName("recording", "fc", m_recording.recordingFrequency, m_recording.bandwidth));
  }
  m_tb->start();
}

Recorder::~Recorder() {
  Logger::info(LABEL, "stop recorder, frequency: {}, time: {} ms", formatFrequency(m_recording.recordingFrequency, RED), getDuration().count());
  if (m_rawFileSinkBlock) {
    m_rawFileSinkBlock->stopRecording();
  }
  m_tb->stop();
  m_tb->wait();
}

Recording Recorder::getRecording() const { return m_recording; }

void Recorder::flush() {
  m_lastDataTime = getTime();
  if (m_rawFileSinkBlock) {
    m_rawFileSinkBlock->flush();
  }
  m_buffer->popSingleSample([this](const SimpleComplex* data, const int size, const std::chrono::milliseconds& time) {
    TransmissionQuery transmission(m_recording.source, m_recording.name, time, m_recording.recordingFrequency, m_recording.bandwidth, m_recording.modulation, encode_base64(data, size));
    m_send(transmission);
  });
}

std::chrono::milliseconds Recorder::getDuration() const { return m_lastDataTime - m_firstDataTime; }

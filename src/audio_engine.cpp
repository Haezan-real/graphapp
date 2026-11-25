#include "audio_engine.h"

#include <QAudio>
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {
constexpr int kRate = 48000;
constexpr double kAuditionHz = 440.0;
constexpr int kFadeFrames = 240;
} // namespace

class AudioEngine::ToneSource : public QIODevice {
public:
  explicit ToneSource(AudioEngine* engine) : engine_(engine) {
    open(QIODevice::ReadOnly);
  }

  bool isSequential() const override { return true; }
  qint64 bytesAvailable() const override { return 48000 * 4; }

  qint64 readData(char* data, qint64 maxlen) override {
    const int channels = std::max(1, engine_->channels_.load());
    const int kind = engine_->sampleKind_.load();
    const int bytesPerSample = kind == 1 ? 4 : 2;
    const int frameBytes = bytesPerSample * channels;
    if (maxlen < frameBytes)
      return 0;
    const int frames = static_cast<int>(maxlen / frameBytes);

    std::shared_ptr<const Program> program;
    {
      std::lock_guard<std::mutex> lock(engine_->programMutex_);
      program = engine_->program_;
    }
    const float boundA = engine_->clipA_.load(std::memory_order_relaxed);
    const float boundB = engine_->clipB_.load(std::memory_order_relaxed);
    const double lo = std::min(boundA, boundB);
    const double hi = std::max(boundA, boundB);
    const double half = 0.5 * (hi - lo);
    const double mid = 0.5 * (hi + lo);
    const std::uint64_t start = engine_->frame_.fetch_add(static_cast<std::uint64_t>(frames));
    const int rate = std::max(1, engine_->rate_.load());

    for (int i = 0; i < frames; ++i) {
      const std::uint64_t frame = start + static_cast<std::uint64_t>(i);
      const double t = static_cast<double>(frame) / static_cast<double>(rate);
      const double x = 2.0 * 3.14159265358979323846 * kAuditionHz * t;
      double y = 0.0;
      if (program)
        program->eval(x, y);
      if (!std::isfinite(y))
        y = 0.0;
      y = std::clamp(y, lo, hi);
      double sample = half > 1e-8 ? (y - mid) / half : 0.0;
      if (frame < static_cast<std::uint64_t>(kFadeFrames))
        sample *= static_cast<double>(frame) / static_cast<double>(kFadeFrames);
      sample = std::clamp(sample, -1.0, 1.0);
      if (kind == 1) {
        auto* out = reinterpret_cast<float*>(data);
        for (int channel = 0; channel < channels; ++channel)
          out[i * channels + channel] = static_cast<float>(sample);
      } else {
        auto* out = reinterpret_cast<std::int16_t*>(data);
        const auto pcm = static_cast<std::int16_t>(std::lrint(sample * 32767.0));
        for (int channel = 0; channel < channels; ++channel)
          out[i * channels + channel] = pcm;
      }
    }
    return static_cast<qint64>(frames) * frameBytes;
  }

  qint64 writeData(const char*, qint64) override { return 0; }

private:
  AudioEngine* engine_ = nullptr;
};

AudioEngine::AudioEngine(QObject* parent) : QObject(parent) {}

AudioEngine::~AudioEngine() {
  stop();
}

void AudioEngine::play(const std::shared_ptr<const Program>& program, double clipA, double clipB) {
  setClip(clipA, clipB);
  setProgram(program);
  frame_.store(0);
  if (playing_)
    return;

  const QAudioDevice device = QMediaDevices::defaultAudioOutput();
  if (device.isNull())
    return;

  QAudioFormat requested;
  requested.setSampleRate(kRate);
  requested.setChannelCount(2);
  requested.setSampleFormat(QAudioFormat::Int16);
  QAudioFormat format = device.isFormatSupported(requested) ? requested : device.preferredFormat();
  if (format.sampleFormat() != QAudioFormat::Int16 && format.sampleFormat() != QAudioFormat::Float)
    format = requested;
  sampleKind_.store(format.sampleFormat() == QAudioFormat::Float ? 1 : 0);
  channels_.store(std::max(1, format.channelCount()));
  rate_.store(std::max(1, format.sampleRate()));

  if (!source_)
    source_ = new ToneSource(this);
  if (sink_) {
    sink_->stop();
    delete sink_;
    sink_ = nullptr;
  }
  sink_ = new QAudioSink(device, format, this);
  sink_->setVolume(1.0f);
  sink_->start(source_);
  playing_ = sink_->state() != QAudio::StoppedState;
}

void AudioEngine::stop() {
  playing_ = false;
  if (sink_)
    sink_->stop();
  {
    std::lock_guard<std::mutex> lock(programMutex_);
    program_.reset();
  }
}

void AudioEngine::setProgram(const std::shared_ptr<const Program>& program) {
  std::lock_guard<std::mutex> lock(programMutex_);
  program_ = program;
}

void AudioEngine::setClip(double clipA, double clipB) {
  clipA_.store(static_cast<float>(clipA));
  clipB_.store(static_cast<float>(clipB));
}

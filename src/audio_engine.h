#pragma once

#include "expr.h"

#include <QObject>
#include <atomic>
#include <memory>
#include <mutex>

class QAudioSink;
class QIODevice;

// Plays a formula as audio. The variable advances as 2*pi*440*t seconds,
// so sin(x) is a 440 Hz tone. Values are clipped to the two horizontal bounds
// and then scaled so that span maps onto full speaker range.
class AudioEngine : public QObject {
public:
  explicit AudioEngine(QObject* parent = nullptr);
  ~AudioEngine() override;

  void play(const std::shared_ptr<const Program>& program, double clipA, double clipB);
  void stop();
  void setProgram(const std::shared_ptr<const Program>& program);
  void setClip(double clipA, double clipB);
  void setVolume(double volume);
  bool isPlaying() const { return playing_; }

private:
  class ToneSource;

  QAudioSink* sink_ = nullptr;
  ToneSource* source_ = nullptr;
  bool playing_ = false;

  std::mutex programMutex_;
  std::shared_ptr<const Program> program_;
  std::atomic<float> clipA_{-1.f};
  std::atomic<float> clipB_{1.f};
  std::atomic<float> volume_{1.f};
  std::atomic<std::uint64_t> frame_{0};
  std::atomic<int> sampleKind_{0};
  std::atomic<int> channels_{2};
  std::atomic<int> rate_{48000};
};

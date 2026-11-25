#pragma once

#include <QWidget>

class AudioEngine;
class FunctionCard;
class GraphWidget;
class TitleBar;

class MainWindow : public QWidget {
  Q_OBJECT
public:
  explicit MainWindow();

  bool isCustomMaximized() const { return maximized_; }
  void restoreFromTitleDrag(const QPoint& globalPos);

protected:
  void showEvent(QShowEvent* event) override;
  void changeEvent(QEvent* event) override;
  bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private:
  void toggleMaximized();
  void minimizeWindow();
  void makeNormalWindow();

  TitleBar* title_ = nullptr;
  GraphWidget* graph_ = nullptr;
  AudioEngine* audio_ = nullptr;
  FunctionCard* playing_ = nullptr;
  bool maximized_ = false;
  bool styled_ = false;
  QRect restore_;
};

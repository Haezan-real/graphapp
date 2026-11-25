#pragma once

#include <QWidget>

class TitleBar : public QWidget {
  Q_OBJECT
public:
  explicit TitleBar(QWidget* parent = nullptr);

  void setClipEditing(bool editing);
  void cancelDrag();

signals:
  void clipEditToggled(bool editing);
  void volumeChanged(double volume);
  void minimizeRequested();
  void maximizeRequested();
  void closeRequested();
  void titlePressed(QPoint globalPos);

protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  void toggleVolume();
  class GlyphButton;
  GlyphButton* minimize_ = nullptr;
  GlyphButton* maximize_ = nullptr;
  GlyphButton* close_ = nullptr;
  GlyphButton* pencil_ = nullptr;
  GlyphButton* volume_ = nullptr;
  QWidget* volumePopup_ = nullptr;
  bool dragging_ = false;
  bool clipEditing_ = false;
  QPointF grab_;
  QPoint windowGrab_;
};

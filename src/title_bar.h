#pragma once

#include <QColor>
#include <QVector>
#include <QWidget>

class TitleBar : public QWidget {
  Q_OBJECT
public:
  explicit TitleBar(QWidget* parent = nullptr);

  struct GraphRow {
    QString name;
    QColor color;
    bool visible = true;
  };

  void setClipEditing(bool editing);
  void cancelDrag();
  void setGraphRows(const QVector<GraphRow>& rows);

signals:
  void clipEditToggled(bool editing);
  void volumeChanged(double volume);
  void graphVisibilityToggled(int index);
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
  void toggleGraphList();
  void rebuildGraphList();
  class GlyphButton;
  GlyphButton* minimize_ = nullptr;
  GlyphButton* maximize_ = nullptr;
  GlyphButton* close_ = nullptr;
  GlyphButton* pencil_ = nullptr;
  GlyphButton* volume_ = nullptr;
  GlyphButton* graphs_ = nullptr;
  QWidget* volumePopup_ = nullptr;
  QWidget* graphPopup_ = nullptr;
  QWidget* graphListBody_ = nullptr;
  QVector<GraphRow> graphRows_;
  bool dragging_ = false;
  bool clipEditing_ = false;
  QPointF grab_;
  QPoint windowGrab_;
};

#pragma once

#include "expr.h"

#include <QColor>
#include <QWidget>

#include <memory>

class FormulaBridge;
class QWebEngineView;

class FunctionCard : public QWidget {
  Q_OBJECT
public:
  static constexpr int kCardWidth = 380;
  static constexpr int kCardHeight = 236;

  FunctionCard(const QColor& color, const QString& name, const QString& body, QWidget* parent = nullptr);
  ~FunctionCard() override;

  const QColor& color() const { return color_; }
  bool selected() const { return selected_; }
  void setSelected(bool selected);
  bool playing() const { return playing_; }
  void setPlaying(bool playing);
  bool wasPlaced() const { return placed_; }
  void markPlaced() { placed_ = true; }

  std::shared_ptr<const Program> program() const { return program_; }
  QString nameText() const { return name_; }
  QString bodyText() const { return body_; }

signals:
  void formulaChanged();
  void playPressed();
  void closeRequested();

protected:
  void resizeEvent(QResizeEvent* event) override;
  void showEvent(QShowEvent* event) override;

private:
  void buildPage();
  void attachChrome();
  void syncOverlay();
  void applySource(const QString& source);
  void applyFormula(const QString& name, const QString& body);
  void publishError();
  QString sourceText() const;
  static QColor paperFor(const QColor& ink);
  void beginDrag();
  void dragBy(const QPoint& total);
  void emitPlay();
  void emitClose();

  struct Chrome;

  QColor color_;
  QColor paper_;
  bool selected_ = false;
  bool playing_ = false;
  bool placed_ = false;
  bool chromeAttached_ = false;
  char variable_ = 'x';
  QString name_;
  QString body_;
  QString error_;
  std::shared_ptr<const Program> program_;
  QPoint dragOrigin_;

  QWidget* host_ = nullptr;
  QWebEngineView* web_ = nullptr;
  FormulaBridge* bridge_ = nullptr;
  std::unique_ptr<Chrome> chrome_;
};

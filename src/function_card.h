#pragma once

#include "expr.h"

#include <QColor>
#include <QWidget>

#include <memory>

class FormulaBridge;
class QLineEdit;
class QWebEngineView;

class FunctionCard : public QWidget {
  Q_OBJECT
public:
  static constexpr int kCardWidth = 240;
  static constexpr int kCardHeight = 108;
  static constexpr int kMinCardWidth = 168;
  static constexpr int kMaxCardWidth = 556;
  static constexpr int kMinCardHeight = 96;

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
  bool graphVisible() const { return graphVisible_; }
  void setGraphVisible(bool visible);

signals:
  void formulaChanged();
  void playPressed();
  void closeRequested();
  void deleteRequested();
  void visibilityChanged(bool visible);
  void layoutChanged();

protected:
  void resizeEvent(QResizeEvent* event) override;
  void showEvent(QShowEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

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
  void placeControls();
  void fitToContent(int contentWidth, int contentHeight);
  void showInPage(const QString& text, int caret);
  void focusEditor(int localX);

  class CardButton;
  struct Chrome;

  QColor color_;
  QColor paper_;
  bool selected_ = false;
  bool playing_ = false;
  bool placed_ = false;
  bool graphVisible_ = true;
  bool chromeAttached_ = false;
  char variable_ = 'x';
  QString name_;
  QString body_;
  QString error_;
  std::shared_ptr<const Program> program_;
  QPoint dragOrigin_;

  QWidget* host_ = nullptr;
  QWebEngineView* web_ = nullptr;
  QLineEdit* editor_ = nullptr;
  CardButton* playButton_ = nullptr;
  CardButton* eyeButton_ = nullptr;
  CardButton* deleteButton_ = nullptr;
  CardButton* closeButton_ = nullptr;
  FormulaBridge* bridge_ = nullptr;
  std::unique_ptr<Chrome> chrome_;
};

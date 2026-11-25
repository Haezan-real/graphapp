#include "function_card.h"

#include <QApplication>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPointer>
#include <QCursor>
#include <QEnterEvent>
#include <QFile>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <functional>
#include <QPalette>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QUrl>
#include <QWebChannel>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>
#include <QWebEngineProfile>
#include <QWebEngineView>

#include <visage/app.h>

#include <algorithm>
#include <atomic>
#include <cmath>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

namespace {

struct Hotspots {
  float playX = 0;
  float playY = 0;
  float closeX = 0;
  float closeY = 0;
  float diameter = 22.f;
  float dragBand = 40.f;
};

Hotspots hotspots(float width) {
  Hotspots hot;
  hot.closeX = width - 10.f - hot.diameter;
  hot.playX = hot.closeX - 8.f - hot.diameter;
  hot.playY = 10.f;
  hot.closeY = hot.playY;
  return hot;
}

} // namespace

class FormulaBridge : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;

  QString pendingSource;
  QString pendingError;

signals:
  void formulaChanged(const QString& source);
  void errorChanged(const QString& message);
  void edited(const QString& source);
  void fitted(int width, int height);

public slots:
  void update(const QString& source) { emit edited(source); }
  void fit(int width, int height) { emit fitted(width, height); }
  void ready() {
    emit formulaChanged(pendingSource);
    emit errorChanged(pendingError);
  }
};

struct FunctionCard::Chrome {
  explicit Chrome(FunctionCard* owner) : card(owner) {
    app.onDraw() = [this](visage::Canvas& canvas) { paint(canvas); };
    app.onMouseDown() = [this](const visage::MouseEvent& event) { pressed(event); };
    app.onMouseUp() = [this](const visage::MouseEvent& event) { released(event); };
    app.onMouseDrag() = [this](const visage::MouseEvent& event) { dragged(event); };
    app.onMouseMove() = [this](const visage::MouseEvent& event) { moved(event); };
    app.onMouseExit() = [this](const visage::MouseEvent&) {
      if (hover.exchange(0) != 0)
        app.redraw();
    };
  }

  void paint(visage::Canvas& canvas) {
    const float w = app.width();
    const float h = app.height();
    const unsigned paper = 0xff000000u | (static_cast<unsigned>(paperRed.load()) << 16) |
                           (static_cast<unsigned>(paperGreen.load()) << 8) |
                           static_cast<unsigned>(paperBlue.load());
    canvas.setColor(paper);
    canvas.rectangle(0, 0, w, h);
  }

  void pressed(const visage::MouseEvent& event) {
    if (!event.isLeftButton())
      return;
    const Hotspots hot = hotspots(app.width());
    const float y = event.position.y;
    if (y <= hot.dragBand) {
      armed = 3;
      dragging = true;
      draggedFar = false;
      grab = QCursor::pos();
      card->beginDrag();
    } else {
      armed = 0;
    }
  }

  void dragged(const visage::MouseEvent&) {
    if (!dragging)
      return;
    const QPoint delta = QCursor::pos() - grab;
    if (delta.manhattanLength() > 3)
      draggedFar = true;
    card->dragBy(delta);
  }

  void released(const visage::MouseEvent&) {
    armed = 0;
    dragging = false;
  }

  void moved(const visage::MouseEvent&) {}

  void attach(HWND parent) {
    if (attached || parent == nullptr)
      return;
    RECT rect {};
    GetClientRect(parent, &rect);
    const int w = std::max(1L, rect.right - rect.left);
    const int h = std::max(1L, rect.bottom - rect.top);
    app.show(visage::Dimension::nativePixels(static_cast<float>(w)),
             visage::Dimension::nativePixels(static_cast<float>(h)), parent);
    attached = app.window() != nullptr;
    resize(parent);
    app.redraw();
    if (QWidget* page = card->findChild<QWebEngineView*>())
      page->raise();
  }

  void resize(HWND parent) {
    if (!attached || app.window() == nullptr || parent == nullptr)
      return;
    RECT rect {};
    GetClientRect(parent, &rect);
    auto* hwnd = static_cast<HWND>(app.window()->nativeHandle());
    SetWindowPos(hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  }

  void syncFlags() { app.redraw(); }

  FunctionCard* card = nullptr;
  visage::ApplicationWindow app;
  std::atomic<int> hover { 0 };
  std::atomic<bool> playing { false };
  std::atomic<bool> selected { false };
  std::atomic<int> red { 91 };
  std::atomic<int> green { 140 };
  std::atomic<int> blue { 255 };
  std::atomic<int> paperRed { 244 };
  std::atomic<int> paperGreen { 231 };
  std::atomic<int> paperBlue { 196 };
  int armed = 0;
  bool dragging = false;
  bool draggedFar = false;
  bool attached = false;
  QPoint grab;
};

void FunctionCard::beginDrag() {
  dragOrigin_ = pos();
}

void FunctionCard::dragBy(const QPoint& total) {
  QPoint next = dragOrigin_ + total;
  if (QWidget* parent = parentWidget()) {
    next.setX(std::clamp(next.x(), 16 - width() + 72, std::max(8, parent->width() - 72)));
    next.setY(std::clamp(next.y(), 0, std::max(0, parent->height() - 28)));
  }
  move(next);
  placed_ = true;
}

void FunctionCard::emitPlay() {
  emit playPressed();
}

void FunctionCard::emitClose() {
  hide();
  emit closeRequested();
}

class FunctionCard::CardButton : public QWidget {
 public:
  enum class Kind { Play, Eye, Delete, Close };

  CardButton(Kind kind, QWidget* parent) : QWidget(parent), kind_(kind) {
    setFixedSize(22, 22);
    setAttribute(Qt::WA_NativeWindow);
    setCursor(Qt::PointingHandCursor);
  }

  void setLit(bool lit) {
    if (lit_ == lit)
      return;
    lit_ = lit;
    update();
  }

  QColor color = QColor(0x6c, 0x93, 0xff);
  std::function<void()> clicked;

 protected:
  void enterEvent(QEnterEvent*) override {
    hover_ = true;
    update();
  }

  void leaveEvent(QEvent*) override {
    hover_ = false;
    update();
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton)
      event->accept();
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()) && clicked)
      clicked();
    event->accept();
  }

  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    if (parentWidget())
      painter.fillRect(rect(), parentWidget()->palette().color(QPalette::Window));
    QColor fill(0x2a, 0x24, 0x1c);
    if (kind_ == Kind::Close)
      fill = hover_ ? QColor(0xff, 0x7a, 0x72) : QColor(0xe5, 0x48, 0x4d);
    else if (kind_ == Kind::Play && lit_)
      fill = hover_ ? color.lighter(115) : color;
    else if (hover_)
      fill = QColor(0x3a, 0x34, 0x28);
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.drawEllipse(rect().adjusted(0, 0, -1, -1));

    painter.setPen(QPen(QColor(0xf6, 0xf0, 0xe4), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    const QPointF center = rect().center();
    if (kind_ == Kind::Close) {
      painter.drawLine(center + QPointF(-4.5, 0), center + QPointF(4.5, 0));
    } else if (kind_ == Kind::Delete) {
      painter.drawLine(center + QPointF(-4.2, -4.6), center + QPointF(4.2, -4.6));
      painter.drawLine(center + QPointF(-1.3, -4.6), center + QPointF(-1.3, -6.2));
      painter.drawLine(center + QPointF(1.3, -4.6), center + QPointF(1.3, -6.2));
      painter.drawLine(center + QPointF(-1.3, -6.2), center + QPointF(1.3, -6.2));
      painter.drawRoundedRect(QRectF(center.x() - 3.4, center.y() - 4.2, 6.8, 8.4), 1.0, 1.0);
    } else if (kind_ == Kind::Play) {
      painter.setBrush(QColor(0xf6, 0xf0, 0xe4));
      painter.drawRoundedRect(QRectF(center.x() - 6.2, center.y() - 1.6, 2.6, 3.2), 0.5, 0.5);
      QPainterPath cone;
      cone.moveTo(center + QPointF(-4.0, -1.6));
      cone.lineTo(center + QPointF(-0.6, -4.4));
      cone.lineTo(center + QPointF(-0.6, 4.4));
      cone.lineTo(center + QPointF(-4.0, 1.6));
      cone.closeSubpath();
      painter.drawPath(cone);
      painter.setBrush(Qt::NoBrush);
      painter.drawArc(QRectF(center.x() - 3.4, center.y() - 3.0, 6.0, 6.0), -50 * 16, 100 * 16);
      painter.drawArc(QRectF(center.x() - 5.2, center.y() - 4.6, 9.2, 9.2), -42 * 16, 84 * 16);
    } else {
      painter.drawEllipse(QRectF(center.x() - 6.0, center.y() - 3.2, 12.0, 6.4));
      painter.setBrush(QColor(0xf6, 0xf0, 0xe4));
      painter.drawEllipse(QRectF(center.x() - 1.5, center.y() - 1.5, 3.0, 3.0));
      if (!lit_) {
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(center + QPointF(-6.4, 4.4), center + QPointF(6.4, -4.4));
      }
    }
  }

 private:
  Kind kind_;
  bool hover_ = false;
  bool lit_ = true;
};

QColor FunctionCard::paperFor(const QColor& ink) {
  auto mix = [](int tint, int cream, int tintWeight, int creamWeight) {
    return (tint * tintWeight + cream * creamWeight) / (tintWeight + creamWeight);
  };
  return QColor(mix(ink.red(), 0xf4, 2, 5), mix(ink.green(), 0xe6, 2, 5), mix(ink.blue(), 0xb4, 1, 5));
}

QString FunctionCard::sourceText() const {
  if (name_.isEmpty())
    return body_;
  if (body_.isEmpty())
    return name_;
  return name_ + QStringLiteral(" = ") + body_;
}

FunctionCard::FunctionCard(const QColor& color, const QString& name, const QString& body, QWidget* parent)
    : QWidget(parent), color_(color), paper_(paperFor(color)), name_(name), body_(body) {
  setFixedSize(kCardWidth, kCardHeight);
  setAttribute(Qt::WA_NativeWindow);
  setAutoFillBackground(true);
  QPalette pal = palette();
  pal.setColor(QPalette::Window, paper_);
  setPalette(pal);

  host_ = new QWidget(this);
  host_->setAttribute(Qt::WA_NativeWindow);
  host_->setAutoFillBackground(true);
  host_->setPalette(pal);
  web_ = new QWebEngineView(this);
  web_->setFocusPolicy(Qt::StrongFocus);
  web_->page()->setBackgroundColor(paper_);
  web_->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
  web_->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
  web_->settings()->setAttribute(QWebEngineSettings::ShowScrollBars, false);
  web_->page()->profile()->setHttpCacheType(QWebEngineProfile::NoCache);

  chrome_ = std::make_unique<Chrome>(this);
  chrome_->red = color_.red();
  chrome_->green = color_.green();
  chrome_->blue = color_.blue();
  chrome_->paperRed = paper_.red();
  chrome_->paperGreen = paper_.green();
  chrome_->paperBlue = paper_.blue();

  playButton_ = new CardButton(CardButton::Kind::Play, this);
  eyeButton_ = new CardButton(CardButton::Kind::Eye, this);
  deleteButton_ = new CardButton(CardButton::Kind::Delete, this);
  closeButton_ = new CardButton(CardButton::Kind::Close, this);
  playButton_->color = color_;
  playButton_->setLit(false);
  eyeButton_->setLit(true);
  playButton_->clicked = [this] { emitPlay(); };
  eyeButton_->clicked = [this] { setGraphVisible(!graphVisible_); };
  deleteButton_->clicked = [this] { emit deleteRequested(); };
  closeButton_->clicked = [this] { emitClose(); };

  editor_ = new QLineEdit(this);
  editor_->setFrame(false);
  editor_->setGeometry(0, 0, 1, 1);
  editor_->setFocusPolicy(Qt::StrongFocus);
  editor_->setStyleSheet(QStringLiteral("QLineEdit{background:transparent;border:none;color:transparent;}"));
  editor_->setText(sourceText());
  connect(editor_, &QLineEdit::textEdited, this, [this](const QString& text) {
    showInPage(text, editor_->cursorPosition());
    applySource(text);
  });
  connect(editor_, &QLineEdit::cursorPositionChanged, this, [this](int, int pos) {
    if (!web_)
      return;
    web_->page()->runJavaScript(QStringLiteral("moveCaret(%1);").arg(pos));
  });
  qApp->installEventFilter(this);
  setFocusProxy(editor_);

  const auto spec = parseFunctionName(name_.toStdString());
  if (spec.ok)
    variable_ = spec.variable;
  std::string error;
  program_ = Program::compile(body_.toStdString(), variable_, error);
  if (!program_)
    error_ = QString::fromStdString(error);

  buildPage();
  syncOverlay();
}

FunctionCard::~FunctionCard() {
  if (qApp)
    qApp->removeEventFilter(this);
}

void FunctionCard::buildPage() {
  bridge_ = new FormulaBridge(this);
  bridge_->pendingSource = sourceText();
  bridge_->pendingError = error_;
  connect(bridge_, &FormulaBridge::edited, this, [this](const QString& source) { applySource(source); });
  connect(bridge_, &FormulaBridge::fitted, this, [this](int width, int height) { fitToContent(width, height); });
  connect(web_, &QWebEngineView::loadFinished, this, [this](bool) {
    const QString js = QStringLiteral("document.documentElement.style.setProperty('--paper','%1');"
                                      "document.body.style.background='%1';")
                           .arg(paper_.name(QColor::HexRgb));
    web_->page()->runJavaScript(js);
  });

  QFile channelFile(":/qtwebchannel/qwebchannel.js");
  if (channelFile.open(QIODevice::ReadOnly)) {
    QWebEngineScript script;
    script.setName("qwebchannel");
    script.setSourceCode(QString::fromUtf8(channelFile.readAll()));
    script.setInjectionPoint(QWebEngineScript::DocumentCreation);
    script.setWorldId(QWebEngineScript::MainWorld);
    web_->page()->scripts().insert(script);
  }

  auto* channel = new QWebChannel(web_->page());
  channel->registerObject(QStringLiteral("bridge"), bridge_);
  web_->page()->setWebChannel(channel);

  const QString page = QCoreApplication::applicationDirPath() + "/web/card.html";
  QUrl pageUrl = QUrl::fromLocalFile(page);
  pageUrl.setQuery(QStringLiteral("v=5"));
  web_->load(pageUrl);
}

void FunctionCard::setSelected(bool selected) {
  selected_ = selected;
  if (chrome_) {
    chrome_->selected = selected;
    chrome_->syncFlags();
  }
}

void FunctionCard::setPlaying(bool playing) {
  playing_ = playing;
  if (playButton_)
    playButton_->setLit(playing);
  if (chrome_) {
    chrome_->playing = playing;
    chrome_->syncFlags();
  }
}

void FunctionCard::setGraphVisible(bool visible) {
  if (graphVisible_ == visible)
    return;
  graphVisible_ = visible;
  if (eyeButton_)
    eyeButton_->setLit(visible);
  emit visibilityChanged(visible);
}

void FunctionCard::applySource(const QString& source) {
  QString name = name_;
  QString body = source.trimmed();
  int eq = -1;
  for (int i = 0; i < source.size(); ++i) {
    if (source[i] == QLatin1Char('\\')) {
      ++i;
      continue;
    }
    if (source[i] == QLatin1Char('=')) {
      eq = i;
      break;
    }
  }
  if (eq >= 0) {
    name = source.left(eq).trimmed();
    body = source.mid(eq + 1).trimmed();
  }
  if (name.isEmpty())
    name = QStringLiteral("f(x)");
  applyFormula(name, body);
}

void FunctionCard::applyFormula(const QString& name, const QString& body) {
  const FunctionName spec = parseFunctionName(name.toStdString());
  if (!spec.ok) {
    error_ = QString::fromStdString(spec.error);
    publishError();
    return;
  }
  QString nextBody = body;
  const bool renamed = spec.variable != variable_;
  if (renamed)
    nextBody = QString::fromStdString(rewriteVariable(body.toStdString(), variable_, spec.variable));

  std::string error;
  auto program = Program::compile(nextBody.toStdString(), spec.variable, error);
  if (!program) {
    error_ = QString::fromStdString(error);
    publishError();
    return;
  }

  variable_ = spec.variable;
  name_ = name;
  body_ = nextBody;
  program_ = std::move(program);
  error_.clear();
  if (renamed && bridge_) {
    bridge_->pendingSource = sourceText();
    bridge_->pendingError = error_;
    emit bridge_->formulaChanged(sourceText());
    emit bridge_->errorChanged(QString());
    if (editor_ && editor_->text() != sourceText()) {
      const int pos = editor_->cursorPosition();
      editor_->setText(sourceText());
      editor_->setCursorPosition(std::min(pos, static_cast<int>(editor_->text().size())));
    }
  } else {
    publishError();
  }
  emit formulaChanged();
}

void FunctionCard::publishError() {
  if (!bridge_)
    return;
  bridge_->pendingError = error_;
  emit bridge_->errorChanged(error_);
}

void FunctionCard::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  attachChrome();
  placeControls();
  QTimer::singleShot(0, this, [this] {
    if (editor_)
      editor_->setFocus();
  });
}

bool FunctionCard::eventFilter(QObject* watched, QEvent* event) {
  if (editor_ && watched == web_ && event->type() == QEvent::FocusIn &&
      QApplication::focusWidget() != editor_) {
    QTimer::singleShot(0, this, [this] {
      if (editor_ && QApplication::focusWidget() != editor_)
        editor_->setFocus();
    });
  }
  if (event->type() == QEvent::MouseButtonPress && web_ && editor_ && isVisible()) {
    const auto* mouse = static_cast<const QMouseEvent*>(event);
    if (mouse->button() == Qt::LeftButton) {
      const QPoint global = mouse->globalPosition().toPoint();
      if (web_->rect().contains(web_->mapFromGlobal(global))) {
        const int localX = web_->mapFromGlobal(global).x();
        QTimer::singleShot(0, this, [this, localX] { focusEditor(localX); });
      }
    }
  }
  return QWidget::eventFilter(watched, event);
}

void FunctionCard::focusEditor(int localX) {
  if (!editor_)
    return;
  editor_->setFocus();
  if (!web_)
    return;
  QPointer<FunctionCard> self(this);
  web_->page()->runJavaScript(QStringLiteral("caretIndexAt(%1)").arg(localX),
                              [self](const QVariant& value) {
                                if (!self || !self->editor_)
                                  return;
                                const int pos = std::clamp(value.toInt(), 0, static_cast<int>(self->editor_->text().size()));
                                self->editor_->setCursorPosition(pos);
                              });
}

void FunctionCard::showInPage(const QString& text, int caret) {
  if (!web_)
    return;
  const QByteArray json = QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact);
  web_->page()->runJavaScript(QStringLiteral("showSource(") + QString::fromUtf8(json) +
                              QStringLiteral("[0],") + QString::number(caret) + QStringLiteral(");"));
}

void FunctionCard::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  syncOverlay();
  if (chromeAttached_)
    attachChrome();
}

void FunctionCard::fitToContent(int contentWidth, int contentHeight) {
  const int cardW = std::clamp(contentWidth + 36, kMinCardWidth, kMaxCardWidth);
  const int cardH = std::clamp(contentHeight + 60, kMinCardHeight, 720);
  if (cardW == width() && cardH == height())
    return;
  setFixedSize(cardW, cardH);
  emit layoutChanged();
}

void FunctionCard::placeControls() {
  constexpr int size = 22;
  constexpr int gap = 6;
  constexpr int margin = 10;
  const int right = width() - margin - size;
  const int y = 10;
  auto place = [&](QWidget* button, int slot) {
    button->setGeometry(right - slot * (size + gap), y, size, size);
    button->raise();
  };
  place(closeButton_, 0);
  place(deleteButton_, 1);
  place(eyeButton_, 2);
  place(playButton_, 3);
}

void FunctionCard::syncOverlay() {
  host_->setGeometry(rect());
  web_->setGeometry(18, 42, std::max(40, width() - 36), std::max(40, height() - 60));
  web_->raise();
  if (playButton_)
    placeControls();
}

void FunctionCard::attachChrome() {
#ifdef _WIN32
  if (!chrome_ || !host_)
    return;
  const HWND parent = reinterpret_cast<HWND>(host_->winId());
  if (!chromeAttached_) {
    chrome_->attach(parent);
    chromeAttached_ = chrome_->attached;
  } else {
    chrome_->resize(parent);
  }
#else
  (void)0;
#endif
}

#include "function_card.moc"

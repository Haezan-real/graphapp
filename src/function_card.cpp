#include "function_card.h"

#include <QCoreApplication>
#include <QCursor>
#include <QFile>
#include <QPalette>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QUrl>
#include <QWebChannel>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>
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
  float header = 46.f;
};

Hotspots hotspots(float width) {
  Hotspots hot;
  hot.closeX = width - 12.f - hot.diameter;
  hot.playX = hot.closeX - 8.f - hot.diameter;
  hot.playY = (hot.header - hot.diameter) * 0.5f;
  hot.closeY = hot.playY;
  return hot;
}

bool inCircle(float px, float py, float x, float y, float diameter) {
  const float dx = px - (x + diameter * 0.5f);
  const float dy = py - (y + diameter * 0.5f);
  const float radius = diameter * 0.5f + 3.f;
  return dx * dx + dy * dy <= radius * radius;
}

} // namespace

class FormulaBridge : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;

  QString pendingName;
  QString pendingBody;
  QString pendingError;

signals:
  void formulaChanged(const QString& name, const QString& body);
  void errorChanged(const QString& message);
  void edited(const QString& name, const QString& body);

public slots:
  void update(const QString& name, const QString& body) { emit edited(name, body); }
  void ready() {
    emit formulaChanged(pendingName, pendingBody);
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
    const Hotspots hot = hotspots(w);
    canvas.setColor(0xff1b1e2a);
    canvas.roundedRectangle(0, 0, w, h, 16);
    canvas.setColor(0xff222636);
    canvas.roundedRectangle(0, 0, w, hot.header, 16);
    canvas.setColor(0xff1b1e2a);
    canvas.rectangle(0, hot.header - 16, w, 16);
    canvas.setColor(selected.load() ? 0xff6c93ff : 0xff343a4e);
    canvas.roundedRectangleBorder(1, 1, w - 2, h - 2, 15, 1.2f);

    const int r = red.load();
    const int g = green.load();
    const int b = blue.load();
    const unsigned accent = 0xff000000u | (static_cast<unsigned>(r) << 16) |
                            (static_cast<unsigned>(g) << 8) | static_cast<unsigned>(b);
    canvas.setColor(accent);
    canvas.roundedRectangle(10, 16, 8, 14, 3);

    drawButton(canvas, hot.playX, hot.playY, hot.diameter, hover.load() == 1, playing.load(), true);
    drawButton(canvas, hot.closeX, hot.closeY, hot.diameter, hover.load() == 2, false, false);
  }

  void drawButton(visage::Canvas& canvas, float x, float y, float diameter, bool over, bool active,
                  bool play) {
    canvas.setColor(active ? 0xff2f6fed : (over ? 0xff556084 : 0xff3e4764));
    canvas.circle(x, y, diameter);
    canvas.setColor(0xfff4f6fb);
    const float cx = x + diameter * 0.5f;
    const float cy = y + diameter * 0.5f;
    if (!play) {
      canvas.roundedRectangle(cx - 5.2f, cy - 1.15f, 10.4f, 2.3f, 1.1f);
      return;
    }
    if (active) {
      canvas.roundedRectangle(cx - 3.4f, cy - 3.4f, 6.8f, 6.8f, 1.4f);
      return;
    }
    visage::Path triangle;
    triangle.moveTo(cx - 2.6f, cy - 4.4f);
    triangle.lineTo(cx + 4.4f, cy);
    triangle.lineTo(cx - 2.6f, cy + 4.4f);
    triangle.close();
    canvas.fill(triangle);
  }

  void pressed(const visage::MouseEvent& event) {
    if (!event.isLeftButton())
      return;
    const Hotspots hot = hotspots(app.width());
    const float x = event.position.x;
    const float y = event.position.y;
    if (inCircle(x, y, hot.playX, hot.playY, hot.diameter))
      armed = 1;
    else if (inCircle(x, y, hot.closeX, hot.closeY, hot.diameter))
      armed = 2;
    else if (y <= hot.header) {
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

  void released(const visage::MouseEvent& event) {
    const Hotspots hot = hotspots(app.width());
    const float x = event.position.x;
    const float y = event.position.y;
    const bool play = armed == 1 && inCircle(x, y, hot.playX, hot.playY, hot.diameter) && !draggedFar;
    const bool close = armed == 2 && inCircle(x, y, hot.closeX, hot.closeY, hot.diameter) && !draggedFar;
    armed = 0;
    dragging = false;
    if (play)
      card->emitPlay();
    else if (close)
      card->emitClose();
  }

  void moved(const visage::MouseEvent& event) {
    const Hotspots hot = hotspots(app.width());
    int next = 0;
    if (inCircle(event.position.x, event.position.y, hot.playX, hot.playY, hot.diameter))
      next = 1;
    else if (inCircle(event.position.x, event.position.y, hot.closeX, hot.closeY, hot.diameter))
      next = 2;
    if (hover.exchange(next) != next)
      app.redraw();
  }

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
  emit closeRequested();
}

FunctionCard::FunctionCard(const QColor& color, const QString& name, const QString& body, QWidget* parent)
    : QWidget(parent), color_(color), name_(name), body_(body) {
  setFixedSize(kCardWidth, kCardHeight);
  setAttribute(Qt::WA_NativeWindow);
  setAutoFillBackground(true);
  QPalette pal = palette();
  pal.setColor(QPalette::Window, QColor(0x1b, 0x1e, 0x2a));
  setPalette(pal);

  host_ = new QWidget(this);
  host_->setAttribute(Qt::WA_NativeWindow);
  web_ = new QWebEngineView(this);
  web_->page()->setBackgroundColor(QColor(0x1b, 0x1e, 0x2a));
  web_->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
  web_->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);

  chrome_ = std::make_unique<Chrome>(this);
  chrome_->red = color_.red();
  chrome_->green = color_.green();
  chrome_->blue = color_.blue();

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

FunctionCard::~FunctionCard() = default;

void FunctionCard::buildPage() {
  bridge_ = new FormulaBridge(this);
  bridge_->pendingName = name_;
  bridge_->pendingBody = body_;
  bridge_->pendingError = error_;
  connect(bridge_, &FormulaBridge::edited, this, [this](const QString& name, const QString& body) {
    applyFormula(name, body);
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
  web_->load(QUrl::fromLocalFile(page));
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
  if (chrome_) {
    chrome_->playing = playing;
    chrome_->syncFlags();
  }
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
    bridge_->pendingName = name_;
    bridge_->pendingBody = body_;
    bridge_->pendingError = error_;
    emit bridge_->formulaChanged(name_, body_);
    emit bridge_->errorChanged(QString());
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
  QTimer::singleShot(0, web_, [this] {
    if (web_)
      web_->setFocus();
  });
}

void FunctionCard::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  syncOverlay();
  if (chromeAttached_)
    attachChrome();
}

void FunctionCard::syncOverlay() {
  host_->setGeometry(rect());
  web_->setGeometry(10, 50, std::max(40, width() - 20), std::max(40, height() - 62));
  web_->raise();

#ifdef _WIN32
  const HWND hwnd = reinterpret_cast<HWND>(winId());
  RECT rect {};
  GetClientRect(hwnd, &rect);
  const int radius = std::lround(16.0 * devicePixelRatioF());
  SetWindowRgn(hwnd, CreateRoundRectRgn(0, 0, rect.right + 1, rect.bottom + 1, radius, radius), TRUE);
#endif
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

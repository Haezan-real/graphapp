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

  QString pendingSource;
  QString pendingError;

signals:
  void formulaChanged(const QString& source);
  void errorChanged(const QString& message);
  void edited(const QString& source);

public slots:
  void update(const QString& source) { emit edited(source); }
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
    const Hotspots hot = hotspots(w);
    const unsigned paper = 0xff000000u | (static_cast<unsigned>(paperRed.load()) << 16) |
                           (static_cast<unsigned>(paperGreen.load()) << 8) |
                           static_cast<unsigned>(paperBlue.load());
    canvas.setColor(paper);
    canvas.rectangle(0, 0, w, h);

    const int r = red.load();
    const int g = green.load();
    const int b = blue.load();
    const unsigned accent = 0xff000000u | (static_cast<unsigned>(r) << 16) |
                            (static_cast<unsigned>(g) << 8) | static_cast<unsigned>(b);
    drawSound(canvas, hot.playX, hot.playY, hot.diameter, hover.load() == 1, playing.load(), accent);
    drawMinus(canvas, hot.closeX, hot.closeY, hot.diameter, hover.load() == 2);
  }

  void drawSound(visage::Canvas& canvas, float x, float y, float diameter, bool over, bool active,
                 unsigned accent) {
    canvas.setColor(active ? accent : (over ? 0xff3a3428 : 0xff2a241c));
    canvas.circle(x, y, diameter);
    canvas.setColor(0xfff6f0e4);
    const float cx = x + diameter * 0.5f;
    const float cy = y + diameter * 0.5f;
    canvas.roundedRectangle(cx - 6.6f, cy - 1.7f, 2.8f, 3.4f, 0.6f);
    visage::Path cone;
    cone.moveTo(cx - 4.2f, cy - 1.7f);
    cone.lineTo(cx - 0.6f, cy - 4.8f);
    cone.lineTo(cx - 0.6f, cy + 4.8f);
    cone.lineTo(cx - 4.2f, cy + 1.7f);
    cone.close();
    canvas.fill(cone);
    const float mouthX = cx - 0.2f;
    const float mouthY = cy;
    canvas.arc(mouthX - 3.1f, mouthY - 3.1f, 6.2f, 1.15f, 0.0f, 1.25f, true);
    canvas.arc(mouthX - 5.0f, mouthY - 5.0f, 10.0f, 1.15f, 0.0f, 1.15f, true);
  }

  void drawMinus(visage::Canvas& canvas, float x, float y, float diameter, bool over) {
    canvas.setColor(over ? 0xffff7a72 : 0xffe5484d);
    canvas.circle(x, y, diameter);
    canvas.setColor(0xfffff7f4);
    const float cx = x + diameter * 0.5f;
    const float cy = y + diameter * 0.5f;
    canvas.roundedRectangle(cx - 5.0f, cy - 1.15f, 10.0f, 2.3f, 1.1f);
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
    else if (y <= hot.dragBand) {
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
  emit closeRequested();
}

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
  web_->page()->setBackgroundColor(paper_);
  web_->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
  web_->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);

  chrome_ = std::make_unique<Chrome>(this);
  chrome_->red = color_.red();
  chrome_->green = color_.green();
  chrome_->blue = color_.blue();
  chrome_->paperRed = paper_.red();
  chrome_->paperGreen = paper_.green();
  chrome_->paperBlue = paper_.blue();

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
  bridge_->pendingSource = sourceText();
  bridge_->pendingError = error_;
  connect(bridge_, &FormulaBridge::edited, this, [this](const QString& source) { applySource(source); });
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
  web_->setGeometry(18, 42, std::max(40, width() - 36), std::max(40, height() - 60));
  web_->raise();
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

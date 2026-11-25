#include "main_window.h"

#include "audio_engine.h"
#include "function_card.h"
#include "graph_widget.h"
#include "title_bar.h"

#include <QEvent>
#include <QScreen>
#include <QShowEvent>
#include <QVBoxLayout>

#include <algorithm>

#ifdef _WIN32
#  include <windows.h>
#  include <windowsx.h>
#endif

MainWindow::MainWindow() {
  setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
  setWindowTitle(QStringLiteral("graphapp"));
  setMinimumSize(880, 560);
  resize(1200, 780);

  title_ = new TitleBar(this);
  graph_ = new GraphWidget(this);
  audio_ = new AudioEngine(this);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(title_);
  layout->addWidget(graph_, 1);

  connect(title_, &TitleBar::closeRequested, this, &QWidget::close);
  connect(title_, &TitleBar::minimizeRequested, this, &MainWindow::minimizeWindow);
  connect(title_, &TitleBar::maximizeRequested, this, &MainWindow::toggleMaximized);
  connect(title_, &TitleBar::clipEditToggled, graph_, &GraphWidget::setClipEditing);
  connect(title_, &TitleBar::titlePressed, this, [this](QPoint global) {
    if (maximized_)
      restoreFromTitleDrag(global);
  });

  connect(graph_, &GraphWidget::playRequested, this, [this](FunctionCard* card) {
    if (playing_ == card) {
      audio_->stop();
      playing_ = nullptr;
      graph_->setPlaying(nullptr);
      return;
    }
    if (!card->program())
      return;
    audio_->play(card->program(), graph_->clipA(), graph_->clipB());
    if (!audio_->isPlaying())
      return;
    playing_ = card;
    graph_->setPlaying(card);
  });
  connect(graph_, &GraphWidget::formulaChanged, this, [this](FunctionCard* card) {
    if (card == playing_)
      audio_->setProgram(card->program());
  });
  connect(graph_, &GraphWidget::clipRangeChanged, this, [this](double a, double b) {
    audio_->setClip(a, b);
  });
  audio_->setClip(graph_->clipA(), graph_->clipB());
}

void MainWindow::makeNormalWindow() {
#ifdef _WIN32
  HWND hwnd = reinterpret_cast<HWND>(winId());
  LONG style = GetWindowLong(hwnd, GWL_STYLE);
  style |= WS_OVERLAPPED | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU;
  style &= ~WS_POPUP;
  SetWindowLong(hwnd, GWL_STYLE, style);
  LONG ex = GetWindowLong(hwnd, GWL_EXSTYLE);
  ex &= ~(WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
  ex |= WS_EX_APPWINDOW;
  SetWindowLong(hwnd, GWL_EXSTYLE, ex);
  SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
#else
  setWindowFlag(Qt::WindowStaysOnTopHint, false);
#endif
}

void MainWindow::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  if (!styled_) {
    styled_ = true;
    makeNormalWindow();
  }
}

void MainWindow::changeEvent(QEvent* event) {
  if (event->type() == QEvent::ActivationChange && !isActiveWindow()) {
    title_->cancelDrag();
#ifdef _WIN32
    ReleaseCapture();
#endif
  }
  QWidget::changeEvent(event);
}

void MainWindow::minimizeWindow() {
#ifdef _WIN32
  ShowWindow(reinterpret_cast<HWND>(winId()), SW_MINIMIZE);
#else
  showMinimized();
#endif
}

void MainWindow::toggleMaximized() {
#ifdef _WIN32
  HWND hwnd = reinterpret_cast<HWND>(winId());
  if (IsZoomed(hwnd)) {
    ShowWindow(hwnd, SW_RESTORE);
    maximized_ = false;
    return;
  }
  restore_ = geometry();
  ShowWindow(hwnd, SW_MAXIMIZE);
  maximized_ = true;
#else
  if (maximized_) {
    maximized_ = false;
    setGeometry(restore_.isValid() ? restore_ : QRect(80, 60, 1200, 780));
    return;
  }
  restore_ = geometry();
  maximized_ = true;
  if (QScreen* screen = this->screen())
    setGeometry(screen->availableGeometry());
#endif
}

void MainWindow::restoreFromTitleDrag(const QPoint& globalPos) {
  if (!maximized_)
    return;
  const QRect previous = restore_.isValid() ? restore_ : QRect(0, 0, 1200, 780);
  const double ratio = width() > 0 ? (globalPos.x() - x()) / double(width()) : 0.5;
  maximized_ = false;
  const int nextX = globalPos.x() - int(std::clamp(ratio, 0.0, 1.0) * previous.width());
#ifdef _WIN32
  ShowWindow(reinterpret_cast<HWND>(winId()), SW_RESTORE);
#endif
  setGeometry(nextX, globalPos.y() - 18, previous.width(), previous.height());
}

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
#ifdef _WIN32
  if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG")
    return QWidget::nativeEvent(eventType, message, result);
  auto* msg = static_cast<MSG*>(message);
  if (msg->message == WM_NCCALCSIZE && msg->wParam == TRUE) {
    auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam);
    if (IsZoomed(msg->hwnd)) {
      HMONITOR monitor = MonitorFromWindow(msg->hwnd, MONITOR_DEFAULTTONEAREST);
      MONITORINFO info {};
      info.cbSize = sizeof(info);
      if (GetMonitorInfo(monitor, &info))
        params->rgrc[0] = info.rcWork;
    }
    *result = 0;
    return true;
  }
  if (msg->message == WM_WINDOWPOSCHANGING) {
    auto* pos = reinterpret_cast<WINDOWPOS*>(msg->lParam);
    if (pos->hwndInsertAfter == HWND_TOPMOST)
      pos->hwndInsertAfter = HWND_NOTOPMOST;
  }
  if (msg->message == WM_SIZE)
    maximized_ = msg->wParam == SIZE_MAXIMIZED;
  if (msg->message == WM_NCHITTEST && !maximized_ && !IsZoomed(msg->hwnd)) {
    const POINT cursor { GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam) };
    RECT window {};
    GetWindowRect(reinterpret_cast<HWND>(winId()), &window);
    constexpr int border = 6;
    const bool left = cursor.x < window.left + border;
    const bool right = cursor.x >= window.right - border;
    const bool top = cursor.y < window.top + border;
    const bool bottom = cursor.y >= window.bottom - border;
    if (top && left)
      *result = HTTOPLEFT;
    else if (top && right)
      *result = HTTOPRIGHT;
    else if (bottom && left)
      *result = HTBOTTOMLEFT;
    else if (bottom && right)
      *result = HTBOTTOMRIGHT;
    else if (left)
      *result = HTLEFT;
    else if (right)
      *result = HTRIGHT;
    else if (top)
      *result = HTTOP;
    else if (bottom)
      *result = HTBOTTOM;
    else
      return QWidget::nativeEvent(eventType, message, result);
    return true;
  }
#else
  Q_UNUSED(eventType);
  Q_UNUSED(message);
  Q_UNUSED(result);
#endif
  return QWidget::nativeEvent(eventType, message, result);
}

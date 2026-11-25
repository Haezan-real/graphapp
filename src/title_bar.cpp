#include "title_bar.h"

#include <QApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QSlider>
#include <QSvgRenderer>

#include <functional>

namespace {
constexpr int kBarHeight = 42;
}

class TitleBar::GlyphButton : public QWidget {
public:
  enum class Kind { Minimize, Maximize, Close, Pencil, Volume };

  GlyphButton(Kind kind, QWidget* parent) : QWidget(parent), kind_(kind) {
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
  }

  void setActive(bool active) {
    active_ = active;
    update();
  }

  std::function<void()> onClick;

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
  void mouseDoubleClickEvent(QMouseEvent* event) override { event->accept(); }
  void mouseReleaseEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()) && onClick)
      onClick();
  }
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF box = rect().adjusted(1, 1, -1, -1);
    if (kind_ == Kind::Pencil) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(active_ ? QColor(0x2a, 0x45, 0x86) : (hover_ ? QColor(255, 255, 255, 28) : QColor(0, 0, 0, 0)));
      painter.drawRoundedRect(box, 8, 8);
      static const char* kPencil =
          "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='#E6E9F2' "
          "stroke-width='1.7' stroke-linecap='round' stroke-linejoin='round'>"
          "<path d='M12 20h9'/>"
          "<path d='M16.5 3.5a2.12 2.12 0 0 1 3 3L8 18l-4 1 1-4 11.5-11.5z'/></svg>";
      QSvgRenderer renderer { QByteArray(kPencil) };
      renderer.render(&painter, box.adjusted(6, 6, -6, -6));
      return;
    }
    if (kind_ == Kind::Volume) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(active_ ? QColor(0x2a, 0x45, 0x86) : (hover_ ? QColor(255, 255, 255, 28) : QColor(0, 0, 0, 0)));
      painter.drawRoundedRect(box, 8, 8);
      static const char* kSpeaker =
          "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='#E6E9F2' "
          "stroke-width='1.7' stroke-linecap='round' stroke-linejoin='round'>"
          "<path d='M11 5 6 9H3v6h3l5 4V5z'/>"
          "<path d='M15.5 8.5a5 5 0 0 1 0 7'/>"
          "<path d='M18.2 6a8.5 8.5 0 0 1 0 12'/></svg>";
      QSvgRenderer renderer { QByteArray(kSpeaker) };
      renderer.render(&painter, box.adjusted(6, 6, -6, -6));
      return;
    }

    QColor color = QColor(0xff, 0xc1, 0x07);
    if (kind_ == Kind::Maximize)
      color = QColor(0x34, 0xc7, 0x59);
    else if (kind_ == Kind::Close)
      color = QColor(0xff, 0x5f, 0x57);
    if (hover_)
      color = color.lighter(112);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(box);

    painter.setPen(QPen(QColor(40, 24, 20, hover_ ? 220 : 150), 1.3, Qt::SolidLine, Qt::RoundCap));
    const QPointF c = box.center();
    if (kind_ == Kind::Minimize) {
      painter.drawLine(QPointF(c.x() - 3.2, c.y()), QPointF(c.x() + 3.2, c.y()));
    } else if (kind_ == Kind::Maximize) {
      painter.setBrush(Qt::NoBrush);
      painter.drawRoundedRect(QRectF(c.x() - 3.1, c.y() - 3.1, 6.2, 6.2), 1.0, 1.0);
    } else {
      painter.drawLine(QPointF(c.x() - 2.6, c.y() - 2.6), QPointF(c.x() + 2.6, c.y() + 2.6));
      painter.drawLine(QPointF(c.x() + 2.6, c.y() - 2.6), QPointF(c.x() - 2.6, c.y() + 2.6));
    }
  }

private:
  Kind kind_;
  bool hover_ = false;
  bool active_ = false;
};

TitleBar::TitleBar(QWidget* parent) : QWidget(parent) {
  setFixedHeight(kBarHeight);
  setMouseTracking(true);
  minimize_ = new GlyphButton(GlyphButton::Kind::Minimize, this);
  maximize_ = new GlyphButton(GlyphButton::Kind::Maximize, this);
  close_ = new GlyphButton(GlyphButton::Kind::Close, this);
  pencil_ = new GlyphButton(GlyphButton::Kind::Pencil, this);
  minimize_->onClick = [this] { emit minimizeRequested(); };
  maximize_->onClick = [this] { emit maximizeRequested(); };
  close_->onClick = [this] { emit closeRequested(); };
  pencil_->onClick = [this] {
    clipEditing_ = !clipEditing_;
    pencil_->setActive(clipEditing_);
    emit clipEditToggled(clipEditing_);
  };
  pencil_->setToolTip("Audio clip range");

  volume_ = new GlyphButton(GlyphButton::Kind::Volume, this);
  volume_->setToolTip("Volume");
  volume_->onClick = [this] { toggleVolume(); };

  volumePopup_ = new QWidget(window());
  volumePopup_->setFixedSize(168, 46);
  volumePopup_->hide();
  volumePopup_->setStyleSheet(
      "QWidget { background: #1c202c; border: 1px solid #343848; border-radius: 10px; }"
      "QSlider::groove:horizontal { height: 4px; background: #3a4054; border-radius: 2px; }"
      "QSlider::sub-page:horizontal { background: #6c93ff; border-radius: 2px; }"
      "QSlider::handle:horizontal { width: 14px; height: 14px; margin: -6px 0; background: #e7ebf5; "
      "border-radius: 7px; }");
  auto* slider = new QSlider(Qt::Horizontal, volumePopup_);
  slider->setRange(0, 100);
  slider->setValue(100);
  slider->setGeometry(16, 14, 136, 18);
  connect(slider, &QSlider::valueChanged, this, [this](int value) { emit volumeChanged(value / 100.0); });
  qApp->installEventFilter(this);
}

void TitleBar::toggleVolume() {
  if (!volumePopup_ || !volume_)
    return;
  if (volumePopup_->isVisible()) {
    volumePopup_->hide();
    volume_->setActive(false);
    return;
  }
  const QPoint below = volume_->mapTo(window(), QPoint(0, volume_->height() + 8));
  volumePopup_->move(below.x() - (volumePopup_->width() - volume_->width()) / 2, below.y());
  volumePopup_->show();
  volumePopup_->raise();
  volume_->setActive(true);
}

bool TitleBar::eventFilter(QObject* watched, QEvent* event) {
  Q_UNUSED(watched);
  if (event->type() == QEvent::MouseButtonPress && volumePopup_ && volumePopup_->isVisible() && volume_) {
    const auto* mouse = static_cast<QMouseEvent*>(event);
    const QPoint global = mouse->globalPosition().toPoint();
    const bool onPopup = volumePopup_->rect().contains(volumePopup_->mapFromGlobal(global));
    const bool onButton = volume_->rect().contains(volume_->mapFromGlobal(global));
    if (!onPopup && !onButton) {
      volumePopup_->hide();
      volume_->setActive(false);
    }
  }
  return QWidget::eventFilter(watched, event);
}

void TitleBar::setClipEditing(bool editing) {
  clipEditing_ = editing;
  pencil_->setActive(editing);
}

void TitleBar::cancelDrag() {
  dragging_ = false;
}

void TitleBar::resizeEvent(QResizeEvent*) {
  constexpr int side = 16;
  minimize_->raise();
  maximize_->raise();
  close_->raise();
  pencil_->raise();
  volume_->raise();
  constexpr int gap = 8;
  const int y = (height() - side) / 2;
  const int right = width() - 14 - side;
  close_->setGeometry(right, y, side, side);
  maximize_->setGeometry(right - gap - side, y, side, side);
  minimize_->setGeometry(right - (gap + side) * 2, y, side, side);
  constexpr int button = 30;
  constexpr int pairGap = 6;
  const int pair = button * 2 + pairGap;
  const int left = (width() - pair) / 2;
  const int buttonY = (height() - button) / 2;
  pencil_->setGeometry(left, buttonY, button, button);
  volume_->setGeometry(left + button + pairGap, buttonY, button, button);
}

void TitleBar::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.fillRect(rect(), QColor(0x14, 0x16, 0x1e));
  painter.setPen(QColor(0x2a, 0x2e, 0x3c));
  painter.drawLine(0, height() - 1, width(), height() - 1);
  painter.setPen(QColor(0xe7, 0xeb, 0xf5));
  QFont font = painter.font();
  font.setPixelSize(13);
  font.setWeight(QFont::DemiBold);
  font.setFamily("Segoe UI");
  painter.setFont(font);
  painter.drawText(QRect(16, 0, 180, height()), Qt::AlignVCenter | Qt::AlignLeft, "graphapp");
}

void TitleBar::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton)
    return;
  emit titlePressed(event->globalPosition().toPoint());
  dragging_ = true;
  grab_ = event->globalPosition();
  windowGrab_ = window()->pos();
}

void TitleBar::mouseMoveEvent(QMouseEvent* event) {
  if (!dragging_ || !(event->buttons() & Qt::LeftButton))
    return;
  const QPoint delta = (event->globalPosition() - grab_).toPoint();
  window()->move(windowGrab_ + delta);
}

void TitleBar::mouseReleaseEvent(QMouseEvent*) {
  dragging_ = false;
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton)
    emit maximizeRequested();
}

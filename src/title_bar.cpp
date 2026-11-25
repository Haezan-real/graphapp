#include "title_bar.h"

#include <QApplication>
#include <QEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QSlider>
#include <QSvgRenderer>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace {
constexpr int kBarHeight = 42;
}

class TitleBar::GlyphButton : public QWidget {
public:
  enum class Kind { Minimize, Maximize, Close, Pencil, Volume, List };

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
    if (kind_ == Kind::List) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(active_ ? QColor(0x2a, 0x45, 0x86) : (hover_ ? QColor(255, 255, 255, 28) : QColor(0, 0, 0, 0)));
      painter.drawRoundedRect(box, 8, 8);
      static const char* kList =
          "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='#E6E9F2' "
          "stroke-width='1.7' stroke-linecap='round' stroke-linejoin='round'>"
          "<path d='M8 6h13M8 12h13M8 18h13'/>"
          "<path d='M4 6h.01M4 12h.01M4 18h.01'/></svg>";
      QSvgRenderer renderer { QByteArray(kList) };
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

class EyeToggle : public QWidget {
public:
  bool open = true;
  std::function<void()> onClick;

  explicit EyeToggle(QWidget* parent) : QWidget(parent) {
    setFixedSize(28, 28);
    setCursor(Qt::PointingHandCursor);
  }

protected:
  void mouseReleaseEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()) && onClick)
      onClick();
  }

  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(0xe7, 0xeb, 0xf5), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    const QPointF center = rect().center();
    painter.drawEllipse(QRectF(center.x() - 6.2, center.y() - 3.3, 12.4, 6.6));
    painter.setBrush(QColor(0xe7, 0xeb, 0xf5));
    painter.drawEllipse(QRectF(center.x() - 1.6, center.y() - 1.6, 3.2, 3.2));
    if (!open) {
      painter.setBrush(Qt::NoBrush);
      painter.drawLine(center + QPointF(-6.6, 4.6), center + QPointF(6.6, -4.6));
    }
  }
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

  graphs_ = new GlyphButton(GlyphButton::Kind::List, this);
  graphs_->setToolTip("Graphs");
  graphs_->onClick = [this] { toggleGraphList(); };

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

  graphPopup_ = new QWidget(window());
  graphPopup_->setAttribute(Qt::WA_NativeWindow);
  graphPopup_->setMinimumWidth(248);
  graphPopup_->hide();
  graphPopup_->setStyleSheet(
      "QWidget#graphPopup { background: #1c202c; border: 1px solid #343848; border-radius: 10px; }"
      "QScrollArea { background: transparent; border: none; }"
      "QLabel { background: transparent; color: #e7ebf5; border: none; }");
  graphPopup_->setObjectName(QStringLiteral("graphPopup"));
  auto* scroll = new QScrollArea(graphPopup_);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->viewport()->setAutoFillBackground(true);
  scroll->viewport()->setStyleSheet(QStringLiteral("background: #1c202c;"));
  graphListBody_ = new QWidget;
  graphListBody_->setStyleSheet(QStringLiteral("background: transparent;"));
  new QVBoxLayout(graphListBody_);
  scroll->setWidget(graphListBody_);
  auto* popupLayout = new QVBoxLayout(graphPopup_);
  popupLayout->setContentsMargins(6, 6, 6, 6);
  popupLayout->addWidget(scroll);

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
  if (graphPopup_ && graphPopup_->isVisible()) {
    graphPopup_->hide();
    graphs_->setActive(false);
  }
  const QPoint below = volume_->mapTo(window(), QPoint(0, volume_->height() + 8));
  volumePopup_->move(below.x() - (volumePopup_->width() - volume_->width()) / 2, below.y());
  volumePopup_->show();
  volumePopup_->raise();
  volume_->setActive(true);
}

void TitleBar::setGraphRows(const QVector<GraphRow>& rows) {
  graphRows_ = rows;
  if (graphPopup_ && graphPopup_->isVisible())
    rebuildGraphList();
}

void TitleBar::rebuildGraphList() {
  if (!graphListBody_)
    return;
  auto* layout = qobject_cast<QVBoxLayout*>(graphListBody_->layout());
  if (!layout)
    return;
  while (QLayoutItem* item = layout->takeAt(0)) {
    delete item->widget();
    delete item;
  }
  layout->setContentsMargins(4, 4, 4, 4);
  layout->setSpacing(2);
  if (graphRows_.isEmpty()) {
    auto* empty = new QLabel(QStringLiteral("No graphs"), graphListBody_);
    empty->setContentsMargins(8, 6, 8, 6);
    layout->addWidget(empty);
  }
  for (int index = 0; index < graphRows_.size(); ++index) {
    const GraphRow& row = graphRows_[index];
    auto* line = new QWidget(graphListBody_);
    line->setFixedHeight(34);
    auto* pip = new QWidget(line);
    pip->setGeometry(8, 12, 10, 10);
    pip->setStyleSheet(QStringLiteral("background: %1; border-radius: 5px;").arg(row.color.name(QColor::HexRgb)));
    auto* name = new QLabel(row.name, line);
    name->setGeometry(26, 0, 168, 34);
    name->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    auto* eye = new EyeToggle(line);
    eye->open = row.visible;
    eye->move(206, 3);
    eye->onClick = [this, index] { emit graphVisibilityToggled(index); };
    layout->addWidget(line);
  }
  layout->addStretch();
  const int rows = std::max(1, static_cast<int>(graphRows_.size()));
  const int height = std::clamp(rows * 36 + 16, 52, 320);
  graphPopup_->setFixedSize(260, height);
}

void TitleBar::toggleGraphList() {
  if (!graphPopup_ || !graphs_)
    return;
  if (graphPopup_->isVisible()) {
    graphPopup_->hide();
    graphs_->setActive(false);
    return;
  }
  if (volumePopup_ && volumePopup_->isVisible()) {
    volumePopup_->hide();
    volume_->setActive(false);
  }
  rebuildGraphList();
  QWidget* top = window();
  const QPoint below = graphs_->mapTo(top, QPoint(0, graphs_->height() + 8));
  int x = below.x() - (graphPopup_->width() - graphs_->width()) / 2;
  x = std::clamp(x, 8, std::max(8, top->width() - graphPopup_->width() - 8));
  graphPopup_->move(x, below.y());
  graphPopup_->show();
  graphPopup_->raise();
  graphs_->setActive(true);
}

bool TitleBar::eventFilter(QObject* watched, QEvent* event) {
  Q_UNUSED(watched);
  if (event->type() == QEvent::MouseButtonPress) {
    const auto* mouse = static_cast<QMouseEvent*>(event);
    const QPoint global = mouse->globalPosition().toPoint();
    auto dismiss = [&](QWidget* popup, GlyphButton* button) {
      if (!popup || !popup->isVisible() || !button)
        return;
      const bool onPopup = popup->rect().contains(popup->mapFromGlobal(global));
      const bool onButton = button->rect().contains(button->mapFromGlobal(global));
      if (!onPopup && !onButton) {
        popup->hide();
        button->setActive(false);
      }
    };
    dismiss(volumePopup_, volume_);
    dismiss(graphPopup_, graphs_);
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
  graphs_->raise();
  constexpr int gap = 8;
  const int y = (height() - side) / 2;
  const int right = width() - 14 - side;
  close_->setGeometry(right, y, side, side);
  maximize_->setGeometry(right - gap - side, y, side, side);
  minimize_->setGeometry(right - (gap + side) * 2, y, side, side);
  constexpr int button = 30;
  constexpr int pairGap = 6;
  constexpr int cluster = button * 3 + pairGap * 2;
  const int left = (width() - cluster) / 2;
  const int buttonY = (height() - button) / 2;
  pencil_->setGeometry(left, buttonY, button, button);
  volume_->setGeometry(left + button + pairGap, buttonY, button, button);
  graphs_->setGeometry(left + (button + pairGap) * 2, buttonY, button, button);
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

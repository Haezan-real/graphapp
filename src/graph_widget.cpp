#include "graph_widget.h"

#include "expr.h"
#include "function_card.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

const char* kGridVertex = R"glsl(
#version 330 core
layout(location = 0) in vec2 aPos;
void main() { gl_Position = vec4(aPos, 0.0, 1.0); }
)glsl";

const char* kGridFragment = R"glsl(
#version 330 core
uniform vec2 uRes;
uniform vec4 uView;
uniform vec4 uStep;
uniform vec2 uClip;
uniform int uClipOn;
out vec4 fragColor;

float gridLine(float world, float step, float pxPerUnit) {
  if (step <= 0.0) return 0.0;
  float m = mod(world, step);
  if (m < 0.0) m += step;
  float dist = min(m, step - m) * pxPerUnit;
  return 1.0 - smoothstep(0.65, 1.45, dist);
}

void main() {
  float nx = gl_FragCoord.x / uRes.x;
  float ny = gl_FragCoord.y / uRes.y;
  float x = mix(uView.x, uView.y, nx);
  float y = mix(uView.z, uView.w, ny);
  float pxPerX = uRes.x / max(uView.y - uView.x, 1e-12);
  float pxPerY = uRes.y / max(uView.w - uView.z, 1e-12);

  vec3 color = vec3(0.055, 0.063, 0.090);
  float minor = max(gridLine(x, uStep.z, pxPerX), gridLine(y, uStep.w, pxPerY));
  float major = max(gridLine(x, uStep.x, pxPerX), gridLine(y, uStep.y, pxPerY));
  color = mix(color, vec3(0.20, 0.24, 0.32), minor * 0.45);
  color = mix(color, vec3(0.33, 0.38, 0.48), major * 0.72);

  float axis = max(1.0 - smoothstep(0.7, 1.7, abs(x) * pxPerX),
                   1.0 - smoothstep(0.7, 1.7, abs(y) * pxPerY));
  color = mix(color, vec3(0.78, 0.82, 0.90), axis);

  if (uClipOn == 1) {
    float lo = min(uClip.x, uClip.y);
    float hi = max(uClip.x, uClip.y);
    if (y >= lo && y <= hi)
      color = mix(color, vec3(0.42, 0.62, 1.0), 0.18);
  }
  fragColor = vec4(color, 1.0);
}
)glsl";

const char* kCurveVertex = R"glsl(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in float aDist;
uniform vec2 uRes;
out float vDist;
void main() {
  vDist = aDist;
  vec2 ndc = vec2(aPos.x / uRes.x * 2.0 - 1.0, 1.0 - aPos.y / uRes.y * 2.0);
  gl_Position = vec4(ndc, 0.0, 1.0);
}
)glsl";

const char* kCurveFragment = R"glsl(
#version 330 core
in float vDist;
uniform vec4 uColor;
uniform int uMode;
uniform float uRadius;
out vec4 fragColor;
void main() {
  float d = abs(vDist);
  if (uMode == 1) {
    // 1/x falloff. The quad edge is faded so the glow is the curve, not a rectangle.
    float glow = 1.15 / (d + 0.72);
    glow *= 1.0 - smoothstep(uRadius * 0.62, uRadius, d);
    vec3 rgb = uColor.rgb * glow;
    fragColor = vec4(rgb, glow);
    return;
  }
  float alpha = (1.0 - smoothstep(uRadius - 0.85, uRadius + 0.45, d)) * uColor.a;
  if (alpha <= 0.001) discard;
  fragColor = vec4(uColor.rgb, alpha);
}
)glsl";

double niceStep(double span, double pixels, double targetPixels) {
  if (!(span > 0.0) || pixels <= 1.0)
    return 1.0;
  const double raw = span / pixels * targetPixels;
  const double exponent = std::pow(10.0, std::floor(std::log10(raw)));
  const double fraction = raw / exponent;
  const double nice = fraction < 1.5 ? 1.0 : fraction < 3.5 ? 2.0 : fraction < 7.5 ? 5.0 : 10.0;
  return nice * exponent;
}

QString formatNumber(double value) {
  if (std::abs(value) < 1e-10)
    return "0";
  const double magnitude = std::abs(value);
  if (magnitude >= 10000.0 || magnitude < 0.001)
    return QString::number(value, 'g', 4);
  QString text = QString::number(value, 'f', 4);
  while (text.contains('.') && (text.endsWith('0') || text.endsWith('.')))
    text.chop(1);
  return text;
}

const QColor kPalette[] = {
    QColor(0x5b, 0x8c, 0xff), QColor(0xff, 0x6b, 0x8a), QColor(0x3d, 0xdc, 0x97),
    QColor(0xff, 0xb0, 0x20), QColor(0xc0, 0x84, 0xfc), QColor(0x22, 0xd3, 0xee),
};

} // namespace

GraphWidget::GraphWidget(QWidget* parent) : QOpenGLWidget(parent) {
  setMouseTracking(true);
  setFocusPolicy(Qt::WheelFocus);
  QPalette pal = palette();
  pal.setColor(QPalette::Window, QColor(0x0e, 0x10, 0x16));
  setAutoFillBackground(true);
  setPalette(pal);

  timer_ = new QTimer(this);
  timer_->setInterval(16);
  clock_ = new QElapsedTimer();
  clock_->start();
  connect(timer_, &QTimer::timeout, this, &GraphWidget::tick);
}

void GraphWidget::setClipEditing(bool editing) {
  clipEdit_ = editing;
  update();
}

void GraphWidget::setPlaying(FunctionCard* card) {
  playing_ = card;
  for (FunctionCard* item : cards_)
    item->setPlaying(item == card);
}

double GraphWidget::worldX(double sx) const {
  if (width() <= 0)
    return 0.0;
  return xMin_ + (sx / width()) * (xMax_ - xMin_);
}

double GraphWidget::worldY(double sy) const {
  if (height() <= 0)
    return 0.0;
  return yMax_ - (sy / height()) * (yMax_ - yMin_);
}

double GraphWidget::screenXOf(double wx) const {
  return (wx - xMin_) / (xMax_ - xMin_) * width();
}

double GraphWidget::screenYOf(double wy) const {
  return (yMax_ - wy) / (yMax_ - yMin_) * height();
}

void GraphWidget::zoomAt(double sx, double sy, double factor) {
  factor = std::clamp(factor, 0.2, 5.0);
  const double wx = worldX(sx);
  const double wy = worldY(sy);
  const double nextXMin = wx + (targetXMin_ - wx) * factor;
  const double nextXMax = wx + (targetXMax_ - wx) * factor;
  const double nextYMin = wy + (targetYMin_ - wy) * factor;
  const double nextYMax = wy + (targetYMax_ - wy) * factor;
  if (nextXMax - nextXMin < 1e-8 || nextYMax - nextYMin < 1e-8)
    return;
  if (nextXMax - nextXMin > 1e7 || nextYMax - nextYMin > 1e7)
    return;
  targetXMin_ = nextXMin;
  targetXMax_ = nextXMax;
  targetYMin_ = nextYMin;
  targetYMax_ = nextYMax;
  startAnimation();
}

void GraphWidget::panPixels(double dx, double dy) {
  const double wx = width() > 0 ? dx / width() * (xMax_ - xMin_) : 0.0;
  const double wy = height() > 0 ? dy / height() * (yMax_ - yMin_) : 0.0;
  xMin_ -= wx;
  xMax_ -= wx;
  targetXMin_ -= wx;
  targetXMax_ -= wx;
  yMin_ += wy;
  yMax_ += wy;
  targetYMin_ += wy;
  targetYMax_ += wy;
  update();
}

void GraphWidget::scaleAround(bool horizontal, double anchor, double factor) {
  factor = std::clamp(factor, 0.25, 4.0);
  if (horizontal) {
    const double nextMin = anchor + (xMin_ - anchor) * factor;
    const double nextMax = anchor + (xMax_ - anchor) * factor;
    if (nextMax - nextMin < 1e-8 || nextMax - nextMin > 1e7)
      return;
    xMin_ = targetXMin_ = nextMin;
    xMax_ = targetXMax_ = nextMax;
  } else {
    const double nextMin = anchor + (yMin_ - anchor) * factor;
    const double nextMax = anchor + (yMax_ - anchor) * factor;
    if (nextMax - nextMin < 1e-8 || nextMax - nextMin > 1e7)
      return;
    yMin_ = targetYMin_ = nextMin;
    yMax_ = targetYMax_ = nextMax;
  }
  update();
}

void GraphWidget::startAnimation() {
  if (!timer_->isActive())
    timer_->start();
  update();
}

bool GraphWidget::settled() const {
  const double xSpan = std::max(1e-9, std::abs(xMax_ - xMin_));
  const double ySpan = std::max(1e-9, std::abs(yMax_ - yMin_));
  return std::abs(targetXMin_ - xMin_) < xSpan * 1e-4 && std::abs(targetXMax_ - xMax_) < xSpan * 1e-4 &&
         std::abs(targetYMin_ - yMin_) < ySpan * 1e-4 && std::abs(targetYMax_ - yMax_) < ySpan * 1e-4;
}

void GraphWidget::tick() {
  const double dt = std::clamp(clock_->restart() / 1000.0, 0.001, 0.05);
  const double blend = 1.0 - std::exp(-dt * 16.0);
  xMin_ += (targetXMin_ - xMin_) * blend;
  xMax_ += (targetXMax_ - xMax_) * blend;
  yMin_ += (targetYMin_ - yMin_) * blend;
  yMax_ += (targetYMax_ - yMax_) * blend;
  if (settled()) {
    xMin_ = targetXMin_;
    xMax_ = targetXMax_;
    yMin_ = targetYMin_;
    yMax_ = targetYMax_;
    timer_->stop();
  }
  update();
}

QString GraphWidget::nextName() const {
  const QString letters = QStringLiteral("fghpqrsyzuvwabcd");
  for (const QChar letter : letters) {
    bool used = false;
    for (const FunctionCard* card : cards_) {
      const FunctionName spec = parseFunctionName(card->nameText().toStdString());
      if (spec.ok && spec.symbol == letter.toLatin1())
        used = true;
    }
    if (!used)
      return QStringLiteral("%1(x)").arg(letter);
  }
  return QStringLiteral("f(x)");
}

QColor GraphWidget::nextColor() const {
  return kPalette[colorCursor_ % int(sizeof(kPalette) / sizeof(kPalette[0]))];
}

FunctionCard* GraphWidget::createCard(const QString& name, const QString& body) {
  const FunctionName spec = parseFunctionName(name.toStdString());
  const char variable = spec.ok ? spec.variable : 'x';
  QString formula = body;
  if (variable != 'x')
    formula = QString::fromStdString(rewriteVariable(body.toStdString(), 'x', variable));
  auto* card = new FunctionCard(nextColor(), name, formula, this);
  ++colorCursor_;
  cards_.push_back(card);
  connect(card, &FunctionCard::playPressed, this, [this, card] { emit playRequested(card); });
  connect(card, &FunctionCard::formulaChanged, this, [this, card] {
    update();
    emit formulaChanged(card);
  });
  connect(card, &FunctionCard::closeRequested, this, [this, card] {
    card->hide();
    update();
  });
  return card;
}

void GraphWidget::clampCard(FunctionCard* card) const {
  if (!card)
    return;
  QPoint pos = card->pos();
  pos.setX(std::clamp(pos.x(), 16 - card->width() + 72, std::max(8, width() - 72)));
  pos.setY(std::clamp(pos.y(), 0, std::max(0, height() - 28)));
  card->move(pos);
}

void GraphWidget::openCard(FunctionCard* card, const QPoint& suggested) {
  if (!card->wasPlaced()) {
    card->move(suggested);
    card->markPlaced();
  }
  clampCard(card);
  card->show();
  card->raise();
}

void GraphWidget::select(FunctionCard* card) {
  for (FunctionCard* item : cards_)
    item->setSelected(item == card);
  update();
}

void GraphWidget::sample(const FunctionCard* card, QVector<QVector<QPointF>>& strokes) const {
  strokes.clear();
  const std::shared_ptr<const Program> program = card->program();
  if (!program || width() < 2 || height() < 2)
    return;
  const int columns = std::clamp(width(), 2, 4096);
  const double limit = std::abs(yMax_ - yMin_) * 30.0;
  QVector<QPointF> current;
  for (int i = 0; i < columns; ++i) {
    const double sx = columns == 1 ? 0.0 : (double)i / (columns - 1) * (width() - 1);
    double y = 0.0;
    const bool ok = program->eval(worldX(sx), y) && std::isfinite(y) && std::abs(y) <= limit;
    if (!ok) {
      if (current.size() >= 2)
        strokes.push_back(current);
      current.clear();
      continue;
    }
    current.push_back(QPointF(sx, screenYOf(y)));
  }
  if (current.size() >= 2)
    strokes.push_back(current);
}

FunctionCard* GraphWidget::hitCurve(const QPointF& pos, double* distance) const {
  FunctionCard* best = nullptr;
  double bestDistance = 8.0;
  for (FunctionCard* card : cards_) {
    QVector<QVector<QPointF>> strokes;
    sample(card, strokes);
    for (const QVector<QPointF>& stroke : strokes) {
      for (int i = 1; i < stroke.size(); ++i) {
        const QPointF ab = stroke[i] - stroke[i - 1];
        const double length2 = QPointF::dotProduct(ab, ab);
        double t = 0.0;
        if (length2 > 1e-8)
          t = std::clamp(QPointF::dotProduct(pos - stroke[i - 1], ab) / length2, 0.0, 1.0);
        const QPointF projected = stroke[i - 1] + ab * t;
        const double dist = QLineF(pos, projected).length();
        if (dist < bestDistance) {
          bestDistance = dist;
          best = card;
        }
      }
    }
  }
  if (distance)
    *distance = bestDistance;
  return best;
}

QPointF GraphWidget::closestOnCurve(FunctionCard* card, const QPointF& target) const {
  QVector<QVector<QPointF>> strokes;
  sample(card, strokes);
  QPointF best = target;
  double bestDistance = std::numeric_limits<double>::max();
  for (const QVector<QPointF>& stroke : strokes) {
    for (const QPointF& point : stroke) {
      const double dist = QLineF(point, target).length();
      if (dist < bestDistance) {
        bestDistance = dist;
        best = point;
      }
    }
  }
  return best;
}

void GraphWidget::resizeEvent(QResizeEvent* event) {
  QOpenGLWidget::resizeEvent(event);
  if (!seeded_ && width() > 240 && height() > 180) {
    seeded_ = true;
    FunctionCard* card = createCard(QStringLiteral("f(x)"), QStringLiteral("\\sin\\left(x\\right)"));
    const QPoint place(std::max(24, width() / 2 - FunctionCard::kCardWidth / 5),
                       std::max(18, height() / 2 - FunctionCard::kCardHeight / 2 - 36));
    openCard(card, place);
  }
  for (FunctionCard* card : cards_)
    clampCard(card);
}

GLuint GraphWidget::linkProgram(const char* vertexSource, const char* fragmentSource) {
  auto compile = [this](GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
      char log[1024];
      glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
      qWarning("shader: %s", log);
    }
    return shader;
  };
  const GLuint vertex = compile(GL_VERTEX_SHADER, vertexSource);
  const GLuint fragment = compile(GL_FRAGMENT_SHADER, fragmentSource);
  const GLuint program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glLinkProgram(program);
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  GLint ok = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetProgramInfoLog(program, sizeof(log), nullptr, log);
    qWarning("program: %s", log);
  }
  return program;
}

void GraphWidget::initializeGL() {
  initializeOpenGLFunctions();
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  gridProgram_ = linkProgram(kGridVertex, kGridFragment);
  curveProgram_ = linkProgram(kCurveVertex, kCurveFragment);

  const float triangle[] = {-1.f, -1.f, 3.f, -1.f, -1.f, 3.f};
  glGenVertexArrays(1, &gridVao_);
  glGenBuffers(1, &gridVbo_);
  glBindVertexArray(gridVao_);
  glBindBuffer(GL_ARRAY_BUFFER, gridVbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(triangle), triangle, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

  glGenVertexArrays(1, &curveVao_);
  glGenBuffers(1, &curveVbo_);
  glGenBuffers(1, &curveEbo_);
  glBindVertexArray(curveVao_);
  glBindBuffer(GL_ARRAY_BUFFER, curveVbo_);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, curveEbo_);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vert), nullptr);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(Vert), reinterpret_cast<void*>(2 * sizeof(float)));
  glBindVertexArray(0);
}

void GraphWidget::resizeGL(int, int) {}

void GraphWidget::appendSegment(QVector<Vert>& verts, QVector<unsigned>& indices, const QPointF& a,
                                const QPointF& b, float halfWidth) const {
  const QPointF delta = b - a;
  const double length = std::hypot(delta.x(), delta.y());
  if (length < 0.001)
    return;
  const QPointF normal(-delta.y() / length, delta.x() / length);
  const unsigned base = unsigned(verts.size());
  const QPointF offset = normal * double(halfWidth);
  verts.push_back(Vert{float(a.x() + offset.x()), float(a.y() + offset.y()), halfWidth});
  verts.push_back(Vert{float(a.x() - offset.x()), float(a.y() - offset.y()), -halfWidth});
  verts.push_back(Vert{float(b.x() + offset.x()), float(b.y() + offset.y()), halfWidth});
  verts.push_back(Vert{float(b.x() - offset.x()), float(b.y() - offset.y()), -halfWidth});
  indices << base << base + 1 << base + 2 << base + 2 << base + 1 << base + 3;
}

void GraphWidget::drawMesh(const QVector<Vert>& verts, const QVector<unsigned>& indices, const QColor& color,
                           int mode, float radius) {
  if (verts.isEmpty() || indices.isEmpty())
    return;
  const qreal dpr = devicePixelRatioF();
  glUseProgram(curveProgram_);
  glUniform2f(glGetUniformLocation(curveProgram_, "uRes"), float(width() * dpr), float(height() * dpr));
  glUniform4f(glGetUniformLocation(curveProgram_, "uColor"), color.redF(), color.greenF(), color.blueF(),
              color.alphaF());
  glUniform1i(glGetUniformLocation(curveProgram_, "uMode"), mode);
  glUniform1f(glGetUniformLocation(curveProgram_, "uRadius"), radius);
  glBindVertexArray(curveVao_);
  glBindBuffer(GL_ARRAY_BUFFER, curveVbo_);
  glBufferData(GL_ARRAY_BUFFER, verts.size() * int(sizeof(Vert)), verts.constData(), GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, curveEbo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * int(sizeof(unsigned)), indices.constData(),
               GL_DYNAMIC_DRAW);
  glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, nullptr);
}

void GraphWidget::paintGL() {
  QPainter painter(this);
  painter.beginNativePainting();

  const qreal dpr = devicePixelRatioF();
  const int pixelWidth = std::max(1, int(std::lround(width() * dpr)));
  const int pixelHeight = std::max(1, int(std::lround(height() * dpr)));
  glViewport(0, 0, pixelWidth, pixelHeight);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  const double xStep = niceStep(xMax_ - xMin_, width(), 110.0);
  const double yStep = niceStep(yMax_ - yMin_, height(), 80.0);
  const double pxPerX = width() > 0 ? width() / (xMax_ - xMin_) : 1.0;
  const double pxPerY = height() > 0 ? height() / (yMax_ - yMin_) : 1.0;
  const double xMinor = xStep / 5.0 * pxPerX >= 8.0 ? xStep / 5.0 : 0.0;
  const double yMinor = yStep / 5.0 * pxPerY >= 8.0 ? yStep / 5.0 : 0.0;

  glUseProgram(gridProgram_);
  glUniform2f(glGetUniformLocation(gridProgram_, "uRes"), float(pixelWidth), float(pixelHeight));
  glUniform4f(glGetUniformLocation(gridProgram_, "uView"), float(xMin_), float(xMax_), float(yMin_),
              float(yMax_));
  glUniform4f(glGetUniformLocation(gridProgram_, "uStep"), float(xStep), float(yStep), float(xMinor),
              float(yMinor));
  glUniform2f(glGetUniformLocation(gridProgram_, "uClip"), float(clipA_), float(clipB_));
  glUniform1i(glGetUniformLocation(gridProgram_, "uClipOn"), clipEdit_ ? 1 : 0);
  glBindVertexArray(gridVao_);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  const auto buildStrokes = [&](FunctionCard* card, QVector<Vert>& verts, QVector<unsigned>& indices,
                                float halfWidth) {
    QVector<QVector<QPointF>> strokes;
    sample(card, strokes);
    for (const QVector<QPointF>& stroke : strokes) {
      for (int i = 1; i < stroke.size(); ++i)
        appendSegment(verts, indices, stroke[i - 1] * dpr, stroke[i] * dpr, halfWidth);
    }
  };

  FunctionCard* selected = nullptr;
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  for (FunctionCard* card : cards_) {
    if (!card->program())
      continue;
    if (card->selected()) {
      selected = card;
      continue;
    }
    QVector<Vert> verts;
    QVector<unsigned> indices;
    buildStrokes(card, verts, indices, float(1.7 * dpr));
    QColor color = card->color();
    color.setAlphaF(0.95f);
    drawMesh(verts, indices, color, 0, float(1.7 * dpr));
  }

  if (selected && selected->program()) {
    QVector<Vert> glowVerts;
    QVector<unsigned> glowIndices;
    QVector<Vert> coreVerts;
    QVector<unsigned> coreIndices;
    buildStrokes(selected, glowVerts, glowIndices, float(22.0 * dpr));
    buildStrokes(selected, coreVerts, coreIndices, float(2.3 * dpr));
    glBlendFunc(GL_ONE, GL_ONE);
    QColor glow = selected->color();
    glow.setAlphaF(1.0);
    drawMesh(glowVerts, glowIndices, glow, 1, float(22.0 * dpr));
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    drawMesh(coreVerts, coreIndices, selected->color().lighter(120), 0, float(2.15 * dpr));
  }

  if (clipEdit_) {
    QVector<Vert> verts;
    QVector<unsigned> indices;
    const double left = 0.0;
    const double right = width() - 1.0;
    appendSegment(verts, indices, QPointF(left, screenYOf(clipA_)) * dpr, QPointF(right, screenYOf(clipA_)) * dpr,
                  float(1.6 * dpr));
    appendSegment(verts, indices, QPointF(left, screenYOf(clipB_)) * dpr, QPointF(right, screenYOf(clipB_)) * dpr,
                  float(1.6 * dpr));
    drawMesh(verts, indices, QColor(0x9e, 0xc5, 0xff), 0, float(1.6 * dpr));
  }

  glBindVertexArray(0);
  glUseProgram(0);
  painter.endNativePainting();
  drawLabels(painter);
}

void GraphWidget::drawLabels(QPainter& painter) const {
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);
  QFont font("Segoe UI", 9);
  painter.setFont(font);
  painter.setPen(QColor(0xc5, 0xcb, 0xda));

  const double xStep = niceStep(xMax_ - xMin_, width(), 110.0);
  const double yStep = niceStep(yMax_ - yMin_, height(), 80.0);
  const double xAxis = std::clamp(screenYOf(0.0), 14.0, height() - 18.0);
  const double yAxis = std::clamp(screenXOf(0.0), 36.0, width() - 12.0);
  const bool xOnScreen = screenYOf(0.0) >= 0.0 && screenYOf(0.0) <= height();
  const bool yOnScreen = screenXOf(0.0) >= 0.0 && screenXOf(0.0) <= width();

  auto ticks = [](double minValue, double maxValue, double step) {
    QVector<double> values;
    if (!(step > 0.0))
      return values;
    const double first = std::ceil(minValue / step) * step;
    for (double value = first; value <= maxValue + step * 0.5; value += step) {
      if (std::abs(value) < step * 1e-6)
        value = 0.0;
      values.push_back(value);
      if (values.size() > 200)
        break;
    }
    return values;
  };

  painter.setPen(QColor(0x9a, 0xa3, 0xb8));
  for (double value : ticks(xMin_, xMax_, xStep)) {
    const double sx = screenXOf(value);
    if (sx < 4 || sx > width() - 4)
      continue;
    const double sy = xOnScreen ? screenYOf(0.0) : (screenYOf(0.0) < 0 ? 0.0 : height() - 1.0);
    painter.drawLine(QPointF(sx, sy - 4), QPointF(sx, sy + 4));
    const QString label = formatNumber(value);
    const QRectF box(sx - 36, sy + (sy > height() * 0.75 ? -18 : 6), 72, 16);
    painter.drawText(box, Qt::AlignHCenter | Qt::AlignVCenter, label);
  }
  for (double value : ticks(yMin_, yMax_, yStep)) {
    if (std::abs(value) < 1e-10)
      continue;
    const double sy = screenYOf(value);
    if (sy < 4 || sy > height() - 4)
      continue;
    const double sx = yOnScreen ? screenXOf(0.0) : (screenXOf(0.0) < 0 ? 0.0 : width() - 1.0);
    painter.drawLine(QPointF(sx - 4, sy), QPointF(sx + 4, sy));
    const QString label = formatNumber(value);
    const QRectF box(sx - (sx > width() * 0.7 ? 64 : -8), sy - 8, 56, 16);
    painter.drawText(box, Qt::AlignLeft | Qt::AlignVCenter, label);
  }

  if (clipEdit_) {
    painter.setPen(QColor(0x9e, 0xc5, 0xff));
    const double hi = std::max(clipA_, clipB_);
    const double lo = std::min(clipA_, clipB_);
    const double x = std::clamp(yAxis + 10.0, 8.0, width() - 48.0);
    painter.drawText(QRectF(x, screenYOf(hi) - 18, 48, 16), Qt::AlignLeft | Qt::AlignVCenter, "top");
    painter.drawText(QRectF(x, screenYOf(lo) + 2, 64, 16), Qt::AlignLeft | Qt::AlignVCenter, "bottom");
  }
  Q_UNUSED(xAxis);
}

void GraphWidget::mousePressEvent(QMouseEvent* event) {
  lastPos_ = event->position();
  pressPos_ = event->position();
  if (event->button() == Qt::MiddleButton) {
    drag_ = Drag::Pan;
    middleMoved_ = false;
    setCursor(Qt::ClosedHandCursor);
    return;
  }
  if (event->button() != Qt::LeftButton)
    return;

  const QPointF pos = event->position();
  if (clipEdit_) {
    const double da = std::abs(pos.y() - screenYOf(clipA_));
    const double db = std::abs(pos.y() - screenYOf(clipB_));
    if (std::min(da, db) <= 7.0) {
      drag_ = da < db ? Drag::ClipA : Drag::ClipB;
      setCursor(Qt::SizeVerCursor);
      return;
    }
  }

  if (hitCurve(pos)) {
    select(hitCurve(pos));
    drag_ = Drag::None;
    return;
  }

  const double originX = screenXOf(0.0);
  const double originY = screenYOf(0.0);
  const bool nearOrigin = std::hypot(pos.x() - originX, pos.y() - originY) < 16.0;
  const bool nearX = std::abs(pos.y() - originY) <= 7.0 && originY >= -8.0 && originY <= height() + 8.0;
  const bool nearY = std::abs(pos.x() - originX) <= 7.0 && originX >= -8.0 && originX <= width() + 8.0;
  if (!nearOrigin && nearX && (!nearY || std::abs(pos.y() - originY) <= std::abs(pos.x() - originX))) {
    drag_ = Drag::AxisX;
    anchor_ = worldX(pos.x());
    setCursor(Qt::SizeHorCursor);
    return;
  }
  if (!nearOrigin && nearY) {
    drag_ = Drag::AxisY;
    anchor_ = worldY(pos.y());
    setCursor(Qt::SizeVerCursor);
    return;
  }
  select(nullptr);
  drag_ = Drag::None;
}

void GraphWidget::mouseMoveEvent(QMouseEvent* event) {
  const QPointF pos = event->position();
  const QPointF delta = pos - lastPos_;
  if (drag_ == Drag::Pan && (event->buttons() & Qt::MiddleButton)) {
    if ((pos - pressPos_).manhattanLength() > 4.0)
      middleMoved_ = true;
    panPixels(delta.x(), delta.y());
  } else if (drag_ == Drag::AxisX && (event->buttons() & Qt::LeftButton)) {
    scaleAround(true, anchor_, std::exp(-delta.x() * 0.0045));
  } else if (drag_ == Drag::AxisY && (event->buttons() & Qt::LeftButton)) {
    scaleAround(false, anchor_, std::exp(delta.y() * 0.0045));
  } else if ((drag_ == Drag::ClipA || drag_ == Drag::ClipB) && (event->buttons() & Qt::LeftButton)) {
    if (drag_ == Drag::ClipA)
      clipA_ = worldY(pos.y());
    else
      clipB_ = worldY(pos.y());
    emit clipRangeChanged(clipA_, clipB_);
    update();
  } else if (drag_ == Drag::None) {
    const bool nearClip = clipEdit_ && (std::abs(pos.y() - screenYOf(clipA_)) <= 7.0 ||
                                        std::abs(pos.y() - screenYOf(clipB_)) <= 7.0);
    const bool nearX = std::abs(pos.y() - screenYOf(0.0)) <= 7.0 && screenYOf(0.0) >= 0.0 &&
                       screenYOf(0.0) <= height();
    const bool nearY = std::abs(pos.x() - screenXOf(0.0)) <= 7.0 && screenXOf(0.0) >= 0.0 &&
                       screenXOf(0.0) <= width();
    if (nearClip)
      setCursor(Qt::SizeVerCursor);
    else if (nearX)
      setCursor(Qt::SizeHorCursor);
    else if (nearY)
      setCursor(Qt::SizeVerCursor);
    else if (hitCurve(pos))
      setCursor(Qt::PointingHandCursor);
    else
      unsetCursor();
  }
  lastPos_ = pos;
}

void GraphWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::MiddleButton && drag_ == Drag::Pan && !middleMoved_) {
    const QString body = QStringLiteral("\\sin\\left(x\\right)");
    FunctionCard* card = createCard(nextName(), body);
    const QPointF center(width() / 2.0, height() / 2.0);
    QPointF mixed = event->position() * 0.4 + center * 0.6;
    mixed.rx() -= FunctionCard::kCardWidth / 2.0;
    mixed.ry() -= FunctionCard::kCardHeight / 2.0;
    openCard(card, mixed.toPoint());
    select(card);
  }
  drag_ = Drag::None;
  unsetCursor();
}

void GraphWidget::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton)
    return;
  FunctionCard* card = hitCurve(event->position());
  if (!card)
    return;
  select(card);
  if (card->wasPlaced()) {
    openCard(card, card->pos());
    return;
  }
  const QPointF center(width() / 2.0, height() / 2.0);
  const QPointF near = closestOnCurve(card, center);
  QPointF centered(center.x() - FunctionCard::kCardWidth / 2.0, center.y() - FunctionCard::kCardHeight / 2.0);
  QPointF mixed = near * 0.34 + centered * 0.66;
  openCard(card, mixed.toPoint());
}

void GraphWidget::wheelEvent(QWheelEvent* event) {
  double dy = event->pixelDelta().y();
  if (qFuzzyIsNull(dy))
    dy = event->angleDelta().y() / 8.0;
  if (qFuzzyIsNull(dy))
    return;
  zoomAt(event->position().x(), event->position().y(), std::exp(-dy * 0.008));
  event->accept();
}

void GraphWidget::leaveEvent(QEvent*) {
  if (drag_ == Drag::None)
    unsetCursor();
}

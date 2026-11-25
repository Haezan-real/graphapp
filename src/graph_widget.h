#pragma once

#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLWidget>

class FunctionCard;

class GraphWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
  Q_OBJECT
public:
  explicit GraphWidget(QWidget* parent = nullptr);

  void setClipEditing(bool editing);
  void setPlaying(FunctionCard* card);
  double clipA() const { return clipA_; }
  double clipB() const { return clipB_; }

signals:
  void playRequested(FunctionCard* card);
  void formulaChanged(FunctionCard* card);
  void clipRangeChanged(double clipA, double clipB);

protected:
  void initializeGL() override;
  void paintGL() override;
  void resizeGL(int width, int height) override;
  void resizeEvent(QResizeEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void leaveEvent(QEvent* event) override;

private:
  enum class Drag { None, Pan, AxisX, AxisY, ClipA, ClipB };

  struct Vert {
    float x;
    float y;
    float dist;
  };

  double worldX(double screenX) const;
  double worldY(double screenY) const;
  double screenXOf(double worldX) const;
  double screenYOf(double worldY) const;
  void zoomAt(double screenX, double screenY, double factor);
  void panPixels(double dx, double dy);
  void scaleAround(bool horizontal, double anchor, double factor);
  void startAnimation();
  void tick();
  bool settled() const;

  FunctionCard* createCard(const QString& name, const QString& body);
  void openCard(FunctionCard* card, const QPoint& suggested);
  void clampCard(FunctionCard* card) const;
  QString nextName() const;
  QColor nextColor() const;
  FunctionCard* hitCurve(const QPointF& pos, double* distance = nullptr) const;
  QPointF closestOnCurve(FunctionCard* card, const QPointF& target) const;
  void select(FunctionCard* card);
  void sample(const FunctionCard* card, QVector<QVector<QPointF>>& strokes) const;
  void appendSegment(QVector<Vert>& verts, QVector<unsigned>& indices, const QPointF& a, const QPointF& b,
                     float halfWidth) const;
  void drawMesh(const QVector<Vert>& verts, const QVector<unsigned>& indices, const QColor& color, int mode,
                float radius);
  GLuint linkProgram(const char* vertex, const char* fragment);
  void drawLabels(QPainter& painter) const;

  QVector<FunctionCard*> cards_;
  FunctionCard* playing_ = nullptr;
  bool seeded_ = false;
  bool clipEdit_ = false;
  double clipA_ = 1.0;
  double clipB_ = -1.0;
  int colorCursor_ = 0;

  double xMin_ = -7.0;
  double xMax_ = 7.0;
  double yMin_ = -2.6;
  double yMax_ = 2.6;
  double targetXMin_ = -7.0;
  double targetXMax_ = 7.0;
  double targetYMin_ = -2.6;
  double targetYMax_ = 2.6;

  Drag drag_ = Drag::None;
  QPointF lastPos_;
  QPointF pressPos_;
  bool middleMoved_ = false;
  double anchor_ = 0.0;
  class QTimer* timer_ = nullptr;
  class QElapsedTimer* clock_ = nullptr;

  GLuint gridProgram_ = 0;
  GLuint curveProgram_ = 0;
  GLuint gridVao_ = 0;
  GLuint gridVbo_ = 0;
  GLuint curveVao_ = 0;
  GLuint curveVbo_ = 0;
  GLuint curveEbo_ = 0;
};

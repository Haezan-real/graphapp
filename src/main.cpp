#include "expr.h"
#include "main_window.h"

#include <QApplication>
#include <QMessageBox>
#include <QSurfaceFormat>

#ifdef _WIN32
#  include <windows.h>
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif
  QSurfaceFormat format;
  format.setVersion(3, 3);
  format.setProfile(QSurfaceFormat::CompatibilityProfile);
  format.setSwapInterval(1);
  QSurfaceFormat::setDefaultFormat(format);
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

  QApplication application(argc, argv);
  QApplication::setApplicationName(QStringLiteral("graphapp"));
  QApplication::setApplicationDisplayName(QStringLiteral("graphapp"));

  std::string failure;
  if (!Program::selfTest(failure)) {
    QMessageBox::critical(nullptr, QStringLiteral("graphapp"), QString::fromStdString(failure));
    return 1;
  }

  MainWindow window;
  window.show();
  return application.exec();
}

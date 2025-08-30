#include <QApplication>
#include <QDir>
#include <QFile>
#include <QStyleFactory>
#include <QTextStream>

#include "loki/src/threading/thread.h"
#include "view/main_window.h"

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);

  // Set application properties
  app.setApplicationName("ZenPlay");
  app.setApplicationVersion("1.0.0");
  app.setOrganizationName("ZenPlay Team");
  app.setApplicationDisplayName("ZenPlay Media Player");

  // Set a modern dark style
  app.setStyle(QStyleFactory::create("Fusion"));

  // Apply dark theme
  QPalette darkPalette;
  darkPalette.setColor(QPalette::Window, QColor(45, 45, 45));
  darkPalette.setColor(QPalette::WindowText, Qt::white);
  darkPalette.setColor(QPalette::Base, QColor(26, 26, 26));
  darkPalette.setColor(QPalette::AlternateBase, QColor(64, 64, 64));
  darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
  darkPalette.setColor(QPalette::ToolTipText, Qt::white);
  darkPalette.setColor(QPalette::Text, Qt::white);
  darkPalette.setColor(QPalette::Button, QColor(45, 45, 45));
  darkPalette.setColor(QPalette::ButtonText, Qt::white);
  darkPalette.setColor(QPalette::BrightText, Qt::red);
  darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
  darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
  darkPalette.setColor(QPalette::HighlightedText, Qt::black);
  app.setPalette(darkPalette);

  // Load custom stylesheet
  QFile styleFile(":/styles/dark_theme.qss");
  if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
    QTextStream stream(&styleFile);
    QString style = stream.readAll();
    app.setStyleSheet(style);
  }

  // Create and show main window
  zenplay::MainWindow window;
  window.show();

  return app.exec();
}

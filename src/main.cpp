#include <string.h>

#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <iostream>

#include "loki/src/threading/thread.h"

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);

  QWidget window;
  QVBoxLayout* layout = new QVBoxLayout(&window);
  QLabel* label = new QLabel("Hello, World!", &window);
  QPushButton* button = new QPushButton("Click Me", &window);

  layout->addWidget(label);
  layout->addWidget(button);
  window.resize(400, 300);
  window.show();
  return app.exec();
}

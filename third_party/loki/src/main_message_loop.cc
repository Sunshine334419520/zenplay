#include "main_message_loop.h"

#include "logging.h"

namespace loki {

namespace {
MainMessageLoop* g_main_message_loop = nullptr;
}

MainMessageLoop* MainMessageLoop::Get() {
  DCHECK(g_main_message_loop);
  return g_main_message_loop;
}

MainMessageLoop::MainMessageLoop() {
  DCHECK(!g_main_message_loop);
  g_main_message_loop = this;
}

MainMessageLoop::~MainMessageLoop() {
  g_main_message_loop = nullptr;
}

}  // namespace loki

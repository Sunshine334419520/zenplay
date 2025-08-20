#include "post_task_interface.h"

#include "threading/loki_thread.h"

namespace loki {

namespace internal {
std::shared_ptr<loki::SingleThreadTaskRunner> GetTaskRunnerForThread(
    ID identifier) {
  return LokiThread::GetTaskRunnerForThread(identifier);
}
}  // namespace internal

bool PostTask(ID identifier,
              const Location& from_here,
              OnceClosure closure) {
  return LokiThread::PostTask(identifier, from_here, std::move(closure));
}

bool PostDelayedTask(ID identifier,
                     const loki::Location& from_here,
                     loki::OnceClosure task,
                     std::chrono::milliseconds delay) {
  return LokiThread::PostDelayedTask(identifier, from_here, std::move(task),
                                     delay);
}

bool PostNonNestableTask(ID identifier,
                         const loki::Location& from_here,
                         loki::OnceClosure task) {
  return LokiThread::PostNonNestableTask(identifier, from_here,
                                         std::move(task));
}

bool PostNonNestableDelayedTask(ID identifier,
                                const loki::Location& from_here,
                                loki::OnceClosure task,
                                std::chrono::milliseconds delay) {
  return LokiThread::PostNonNestableDelayedTask(identifier, from_here,
                                                std::move(task), delay);
}

bool PostTaskAndReply(ID identifier,
                      const loki::Location& from_here,
                      loki::OnceClosure task,
                      loki::OnceClosure reply) {
  return LokiThread::PostTaskAndReply(identifier, from_here, std::move(task),
                                      std::move(reply));
}

bool CurrentlyOn(ID identifier) {
  return LokiThread::CurrentlyOn(identifier);
}

}  // namespace loki



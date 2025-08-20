/**
* @Author: YangGuang
* @Date:   2018-10-24
* @Email:  guang334419520@126.com
* @Filename: single_thread_task_runner.h
* @Last modified by:  YangGuang
*/
#ifndef BASE_SINGLE_THREAD_TASK_RUNNER_H
#define BASE_SINGLE_THREAD_TASK_RUNNER_H

#include "loki_export.h"
#include "macor.h"
#include "sequenced_task_runner.h"

namespace loki {

class LOKI_EXPORT SingleThreadTaskRunner : public SequencedTaskRunner {
 public:
  bool BelongsToCurrentThread() { return RunsTasksInCurrentSequence(); }

 protected:
  ~SingleThreadTaskRunner() OVERRIDE = default;
};

}  // namespace loki.

#endif // !BASE_SINGLE_THREAD_TASK_RUNNER_H


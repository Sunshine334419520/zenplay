/**
* @Author: YangGuang
* @Date:   2018-10-21
* @Email:  guang334419520@126.com
* @Filename: post_task_and_reply_impl.h
* @Last modified by:  YangGuang
*/

// This file contains the implementation for TaskRunner::PostTaskAndReply.
#pragma once

#include "loki_export.h"
#include "macor.h"
#include "location.h"
#include "callback.h"

namespace loki {

namespace internal {

class LOKI_EXPORT PostTaskAndReplayImpl {
 public:
  PostTaskAndReplayImpl() = default;
  virtual ~PostTaskAndReplayImpl() = default;

  // Posts |task| by calling PostTask(). On completion, posts |reply| to the
  // origin sequence. Can only be called when
  bool PostTaskAndReply(const Location& from_here,
                        OnceClosure task,
                        OnceClosure reply);

 private:
  virtual bool PostTask(const Location& from_here, OnceClosure task) = 0;
};

}  // namespace internal.

}  // namespace loki.
/**
* @Author: YangGuang
* @Date:   2018-10-21
* @Email:  guang334419520@126.com
* @Filename: post_task_and_reply_impl.cc
* @Last modified by:  YangGuang
*/
#include "threading/post_task_and_reply_impl.h"


#include "location.h"
#include "logging.h"
//#include "logging.h"
#include "sequenced_task_runner.h"
#include "sequenced_task_runner_handle.h"
#include "bind_util.h"


namespace loki {

namespace {

class PostTaskAndReplyRelay {
 public:
  PostTaskAndReplyRelay(const Location& from_here,
                        OnceClosure task,
                        OnceClosure reply)
      : from_here_(from_here),
        origin_task_runner_(SequencedTaskRunnerHandle::Get()),
        reply_(std::move(reply)),
        task_(std::move(task)) {
  }

  ~PostTaskAndReplyRelay() {}

  void RunTaskAndPostReply() {
    std::move(task_).Run();

    // 如果调用着是外部自己的线程，这里不应该使用origin_task_runner_.
    origin_task_runner_->PostTask(
        from_here_,
        BindOnceClosure(&PostTaskAndReplyRelay::RunReplyAndSelfDestruct, this));
    // RunReplyAndSelfDestruct();
  }

 private:
  void RunReplyAndSelfDestruct() {
    DCHECK(task_.is_null());

    std::move(reply_).Run();

    delete this;
  }

  const Location from_here_;
  const std::shared_ptr<SequencedTaskRunner> origin_task_runner_;
  OnceClosure reply_;
  OnceClosure task_;
};

}  // namespace

namespace internal {

bool PostTaskAndReplayImpl::PostTaskAndReply(const Location& from_here,
                                             OnceClosure task,
                                             OnceClosure reply) {
  DCHECK(!task.is_null());
  DCHECK(!reply.is_null());

  PostTaskAndReplyRelay* relay =
      new PostTaskAndReplyRelay(from_here, std::move(task), std::move(reply));

  if (!PostTask(from_here,
                BindOnceClosure(&PostTaskAndReplyRelay::RunTaskAndPostReply,
                                relay))) {
    delete relay;
    return false;
  }

  return true;
}

}  // namespace internal

}  // namespace loki

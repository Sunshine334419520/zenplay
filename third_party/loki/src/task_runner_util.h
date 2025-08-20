/**
* @Author: YangGuang
* @Date:   2018-11-13
* @Email:  guang334419520@126.com
* @Filename: task_runner.h
* @Last modified by:  YangGuang
*/
#ifndef BASE_TASK_RUNNER_UTIL_H
#define BASE_TASK_RUNNER_UTIL_H

#include <utility>

#include "callback.h"
#include "bind_util.h"
#include "post_task_and_reply_with_result_internal.h"
#include "task_runner.h"

namespace loki {

// 现在假设你有了一下的函数
// R DoWorkAndReturn();
// void Callback(const R& result);
//
// 这个PostTaskAndReplyWithResult可以这样来使用 for example:
// PostTaskAndReplyWithResult(
//	target_thread_.task_runner(),
//  FROM_HERE,
//  std::bind(&DoWorkAndReturn),
//	std::bind(&Callback));
template <typename TaskReturnType, typename ReplyArgType>
bool PostTaskAndReplyWithResult(TaskRunner* task_runner,
								const Location& from_here,
								loki::Callback<TaskReturnType()> task,
								loki::Callback<void(ReplyArgType)> reply) {
	TaskReturnType* result = new TaskReturnType();
	return task_runner->PostTaskAndReplay(
		from_here,
		loki::BindOnceClosure(&internal::ReturnAsParamAdapter<TaskReturnType>,
		std::move(task), result),
		loki::BindOnceClosure(&internal::ReplayAdapter<TaskReturnType, ReplyArgType>,
		std::move(reply), result));
}

}	// namespace loki.

#endif // !BASE_TASK_RUNNER_UTIL_H
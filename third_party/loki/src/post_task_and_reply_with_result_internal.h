/**
* @Author: YangGuang
* @Date:   2018-11-13
* @Email:  guang334419520@126.com
* @Filename: callback.h
* @Last modified by:  YangGuang
*/
#ifndef BASE_POST_TASK_AND_REPLY_WITH_RESULT_INTERNAL_H
#define BASE_POST_TASK_AND_REPLY_WITH_RESULT_INTERNAL_H

#include <utility>

#include "callback.h"

namespace loki {

namespace internal {

// 这个只是一个帮助函数，因为loki::Closure只支持void().
template <typename ReturnType>
void ReturnAsParamAdapter(loki::Callback<ReturnType()> func,
                          ReturnType* result) {
  *result = std::move(func)();
}

template <typename TaskReturnType, typename ReplyArgType>
void ReplayAdapter(loki::Callback<void(ReplyArgType)> callback,
                   TaskReturnType* result) {
  std::move(callback)(*result);
  delete result;
}

}  // namespace internal
}  // namespace loki

#endif // !BASE_POST_TASK_AND_REPLY_WITH_RESULT_INTERNAL_H

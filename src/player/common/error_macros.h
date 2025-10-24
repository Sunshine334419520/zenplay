#pragma once

#include "player/common/error.h"

/**
 * @file error_macros.h
 * @brief Result<T> 便利宏定义
 *
 * 提供简化错误处理的宏，减少样板代码。
 */

namespace zenplay {

/**
 * @brief 如果 Result 是错误，立即返回该错误
 *
 * 使用场景：需要传播错误的函数
 *
 * 示例：
 * @code
 * Result<void> DoSomething() {
 *   RETURN_IF_ERROR(Step1());  // 如果失败，直接返回错误
 *   RETURN_IF_ERROR(Step2());  // 继续下一步
 *   return Result<void>::Ok();
 * }
 * @endcode
 */
#define RETURN_IF_ERROR(expr)                                        \
  do {                                                               \
    auto __result = (expr);                                          \
    if (__result.IsErr()) {                                          \
      return Result<void>::Err(__result.Code(), __result.Message()); \
    }                                                                \
  } while (0)

/**
 * @brief 如果 Result 是错误，附加额外上下文后返回
 *
 * 使用场景：需要在错误传播时添加更多上下文信息
 *
 * 示例：
 * @code
 * Result<void> OpenFile(const std::string& path) {
 *   RETURN_IF_ERROR_WITH(ValidatePath(path), "Invalid file path: " + path);
 *   RETURN_IF_ERROR_WITH(CheckPermissions(path), "No permission to read file");
 *   return Result<void>::Ok();
 * }
 * @endcode
 */
#define RETURN_IF_ERROR_WITH(expr, message)                                   \
  do {                                                                        \
    auto __result = (expr);                                                   \
    if (__result.IsErr()) {                                                   \
      return Result<void>::Err(                                               \
          __result.Code(), std::string(message) + ": " + __result.Message()); \
    }                                                                         \
  } while (0)

/**
 * @brief 将 bool 返回值转换为 Result<void>
 *
 * 使用场景：调用旧的 bool 返回值 API，需要转换为 Result
 *
 * 示例：
 * @code
 * bool OldApiCall();  // 旧 API
 *
 * Result<void> NewApiCall() {
 *   BOOL_TO_RESULT(OldApiCall(), ErrorCode::kInternalError, "Old API failed");
 *   return Result<void>::Ok();
 * }
 * @endcode
 */
#define BOOL_TO_RESULT(expr, error_code, message)    \
  do {                                               \
    if (!(expr)) {                                   \
      return Result<void>::Err(error_code, message); \
    }                                                \
  } while (0)

/**
 * @brief 检查指针非空，否则返回错误
 *
 * 使用场景：验证指针参数或返回值
 *
 * 示例：
 * @code
 * Result<void> ProcessData(const Data* data) {
 *   CHECK_NOT_NULL(data, "Data pointer is null");
 *   // 使用 data...
 *   return Result<void>::Ok();
 * }
 * @endcode
 */
#define CHECK_NOT_NULL(ptr, message)                                   \
  do {                                                                 \
    if ((ptr) == nullptr) {                                            \
      return Result<void>::Err(ErrorCode::kInvalidParameter, message); \
    }                                                                  \
  } while (0)

/**
 * @brief 从 Result<T> 中提取值，如果是错误则返回错误
 *
 * 使用场景：需要从 Result<T> 中获取值并继续使用
 *
 * 示例：
 * @code
 * Result<int> GetNumber();
 *
 * Result<void> UseNumber() {
 *   ASSIGN_OR_RETURN(int num, GetNumber());
 *   // 现在可以使用 num
 *   std::cout << "Got number: " << num << std::endl;
 *   return Result<void>::Ok();
 * }
 * @endcode
 */
#define ASSIGN_OR_RETURN(var_decl, expr)                     \
  auto __result_##__LINE__ = (expr);                         \
  if (__result_##__LINE__.IsErr()) {                         \
    return Result<void>::Err(__result_##__LINE__.Code(),     \
                             __result_##__LINE__.Message()); \
  }                                                          \
  var_decl = __result_##__LINE__.Value()

}  // namespace zenplay

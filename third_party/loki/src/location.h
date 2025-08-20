/**
* @Author: YangGuang
* @Date:   2018-10-10
* @Email:  guang334419520@126.com
* @Filename: location.h
* @Last modified by:  YangGuang
*/

#pragma once

#include <stddef.h>

#include <cassert>
#include <string>

#include "loki_export.h"
#include "hash.h"

namespace loki {

#if defined(_MSC_VER)
#include <intrin.h>
#define RETURN_ADDRESS() _ReturnAddress()
#elif defined(__GUNC__)
#define RETURN_ADDRESS() \
  __builtin_extract_return_addr(__builtin_return_address(0))
#else
#define RETURN_ADDRESS() nullptr
#endif

class LOKI_EXPORT Location {
 public:
  Location() = default;
  Location(const Location& other) = default;

  // 只初始化file name 和 program counter
  Location(const char* file_name, const void* program_counter)
      : file_name_(file_name), program_counter_(program_counter) {}

  // 应该提供一个长时间存在的char*，如__FILE__.这个值将不会产生复制,
  // 只是简单的指针之间的赋值.
  Location(const char* function_name,
           const char* file_name,
           int line_number,
           const void* program_counter)
      : function_name_(function_name),
        file_name_(file_name),
        line_number_(line_number),
        program_counter_(program_counter) {}

  // 用于hash map insertion的比较器, 这个program counter应该是唯一标识
  // 这个Location.
  bool operator==(const Location& other) const {
    return program_counter_ == other.program_counter_;
  }

  // 如果放回true代表有source code location info. 返回false，
  // 这个Location对象只包含一个program counter,或者是default-initialized.
  bool has_source_info() const { return function_name_ && file_name_; }

  // 如果是default initialize，返回值将是nullptr.
  const char* function_name() const { return function_name_; }

  // 如果是default initialize, 返回值将是nullptr.
  const char* file_name() const { return file_name_; }

  // 如果是default initialize,返回值将是-1.
  int line_number() const { return line_number_; }

  // 这个program_counter应该是一直有效的，除非是defult initialize.
  // 那样返回值将是nullptr.
  const void* program_counter() const { return program_counter_; }

  // 转换成用户最能够看懂的样子, 如果function and filename 是无效的，
  // 这个将 return "pc:<hex address>".
  std::string ToString() const {
    if (has_source_info()) {
      return std::string(function_name_) + "@" + file_name_ + ":" +
             std::to_string(line_number_);
    }
    // return printf("pc:%p", program_counter_);
    return std::string(reinterpret_cast<const char*>(program_counter_));
  }

  static Location CreateFromHere(const char* file_name) {
    return Location(file_name, RETURN_ADDRESS());
  }

  static Location CreateFromHere(const char* function_name,
    const char* file_name,
    int line_number) {
    return Location(function_name, file_name, line_number, RETURN_ADDRESS());
  }

 private:
  const char* function_name_ = nullptr;
  const char* file_name_ = nullptr;
  int line_number_ = -1;
  const void* program_counter_ = nullptr;
};

#define FROM_HERE FROM_HERE_WITH_EXPLICIT_FUNCTION(__FUNCTION__)
#define FROM_HERE_WITH_EXPLICIT_FUNCTION(function_name) \
  ::loki::Location::CreateFromHere(function_name, __FILE__, __LINE__)

}  // namespace loki


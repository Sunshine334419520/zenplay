/**
* @Author: YangGuang
* @Date:   2018-10-23
* @Email:  guang334419520@126.com
* @Filename: sequenced_task_runner_helpers.h
* @Last modified by:  YangGuang
*/
#pragma once

namespace loki {

class SequencedTaskRunner;

template <typename T>
class DeleteHelper {
 private:
  static void DoDelete(const void* object) {
    delete static_cast<const T*>(object);
  }

  friend class SequencedTaskRunner;
};

template <typename T>
class ReleaseHelper {
 private:
  static void DoRelease(const void* object) {
    static_cast<const T*>(object)->Release();
  }

  friend class SequencedTaskRunner;
};

}  // namespace loki.

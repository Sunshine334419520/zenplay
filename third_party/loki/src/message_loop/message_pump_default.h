/**
* @Author: YangGuang
* @Date:   2018-10-15
* @Email:  guang334419520@126.com
* @Filename: message_pump_default.h
* @Last modified by:  YangGuang
*/
#pragma once

#include <chrono>
#include <atomic>
#include <condition_variable>

#include "loki_export.h"
#include "macor.h"
#include "message_loop/message_pump.h"

namespace loki {

class LOKI_EXPORT MessagePumpDefault : public MessagePump {
 public:
  MessagePumpDefault();
  ~MessagePumpDefault() OVERRIDE;

  // MessagePump methods:
  void Run(Delegate* delegate) OVERRIDE;
  void Quit() OVERRIDE;
  void ScheduleWork() OVERRIDE;
  void ScheduleDelayedWork(
      const std::chrono::milliseconds& delayed_time_work) OVERRIDE;

 private:
  // This is flag is set to false when Run should return.
  std::atomic<bool> keep_running_;

  // Used to sleep until there is more work to do.
  std::condition_variable event_;

  std::mutex mutex_;

  // the time at which we should call DodelayedWork.
  std::chrono::milliseconds delayed_work_time_;

  // DISALLOW_COPY_AND_ASSIGN(MessagePumpDefault);
};

}  // namespace loki.

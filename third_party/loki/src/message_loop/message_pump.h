/**
* @Author: YangGuang
* @Date:   2018-10-13
* @Email:  guang334419520@126.com
* @Filename: message_pump.h
* @Last modified by:  YangGuang
*/
#pragma once

#include <chrono>

#include "loki_export.h"

namespace loki {

class LOKI_EXPORT MessagePump {
 public:
  class LOKI_EXPORT Delegate {
   public:
    virtual ~Delegate() {}

    virtual bool DoWork() = 0;

    virtual bool DoDelayedWork(
        std::chrono::milliseconds& next_delayed_work_time) = 0;

    virtual bool DoIdleWork() = 0;
  };

  MessagePump();
  virtual ~MessagePump();

  virtual void Run(Delegate* delegate) = 0;

  virtual void Quit() = 0;

  virtual void ScheduleWork() = 0;

  virtual void ScheduleDelayedWork(
      const std::chrono::milliseconds& delayed_time_work) = 0;

  virtual void SetTimerSlack();
};

}  // namespace loki

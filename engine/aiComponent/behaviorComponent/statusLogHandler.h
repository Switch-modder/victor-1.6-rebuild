/**
 * File: statusLogHandler.h
 *
 * Author: ross
 * Created: 2018-05-07
 *
 * Description: A handler for status messages and a store for recent statuses
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#ifndef __Engine_AiComponent_BehaviorComponent_StatusLogHandler_H__
#define __Engine_AiComponent_BehaviorComponent_StatusLogHandler_H__

#include "engine/engineTimeStamp.h"
#include "util/container/circularBuffer.h"
#include "util/helpers/noncopyable.h"
#include "util/signals/simpleSignal_fwd.h"

namespace Anki {
namespace Vector {

class CozmoContext;
namespace external_interface {
  class Status;
  class TimeStampedStatus;
}

class StatusLogHandler : private Util::noncopyable
{
public:
  explicit StatusLogHandler( const CozmoContext* context );
  
  void SetFeature( const std::string& featureName, const std::string& source );
  
private:
  
  void SaveStatusHistory( const external_interface::TimeStampedStatus& status );

  void SendStatusHistory();
  
  const CozmoContext* _context = nullptr;

  using StatusHistory = Util::CircularBuffer<external_interface::TimeStampedStatus>;
  std::unique_ptr<StatusHistory> _statusHistory;

  std::vector<Signal::SmartHandle> _signalHandles;
};

} // namespace Vector
} // namespace Anki

#endif

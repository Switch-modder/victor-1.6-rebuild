/**
 * File: robotExternalRequestComponent.h
 *
 * Author: Rakesh Ravi Shankar
 * Created: 7/16/18
 *
 * Description: Component to handle external protobuf message requests
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#ifndef __Engine_Components_RobotExternalRequestComponent_H__
#define __Engine_Components_RobotExternalRequestComponent_H__

#include "engine/robot.h"
#include "engine/cozmoContext.h"
#include "engine/cozmoAPI/comms/protoMessageHandler.h"

namespace Anki {
namespace Vector {

class Robot;
template<typename T> class AnkiEvent;
namespace external_interface {
  class GatewayWrapper;
}

class RobotExternalRequestComponent : public IDependencyManagedComponent<RobotComponentID>, private Util::noncopyable
{
public:
  RobotExternalRequestComponent();
  ~RobotExternalRequestComponent() = default;
  void Init(CozmoContext* context);
  void GetVersionState(const AnkiEvent<external_interface::GatewayWrapper>& event);
  void GetBatteryState(const AnkiEvent<external_interface::GatewayWrapper>& event);

private:  
  CozmoContext* _context = nullptr;
};

} // Cozmo namespace
} // Anki namespace

#endif // __Engine_Components_RobotExternalRequestComponent_H__

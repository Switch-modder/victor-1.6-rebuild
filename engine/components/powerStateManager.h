/**
 * File: powerStateManager.h
 *
 * Author: Brad Neuman
 * Created: 2018-06-27
 *
 * Description: Central engine component to manage power states (i.e. "power save mode")
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#ifndef __Engine_Components_PowerStateManager_H__
#define __Engine_Components_PowerStateManager_H__

#include "engine/aiComponent/behaviorComponent/behaviorComponents_fwd.h"
#include "engine/robotComponents_fwd.h"
#include "util/entityComponent/iDependencyManagedComponent.h"
#include "util/helpers/noncopyable.h"
#include "engine/contextWrapper.h"

#include "util/signals/simpleSignal_fwd.h"

#include "clad/types/lcdTypes.h"

#include <set>

namespace Anki {
namespace Vector {

class CozmoContext;
struct RobotState;

enum class PowerSaveSetting {
  CalmMode,
  Camera,
  LCDBacklight,
  ThrottleCPU,
  ProxSensorNavMap,
};

class PowerStateManager : public IDependencyManagedComponent<RobotComponentID>,
                          public UnreliableComponent<BCComponentID>,
                          private Anki::Util::noncopyable
{
public:
  PowerStateManager();

  virtual void GetInitDependencies(RobotCompIDSet& dependencies) const override {
    dependencies.insert(RobotComponentID::CozmoContextWrapper);
  }
  virtual void InitDependent(Vector::Robot* robot, const RobotCompMap& dependentComps) override;

  virtual void GetUpdateDependencies(RobotCompIDSet& dependencies) const override {
    dependencies.insert(RobotComponentID::AIComponent);
  }
  virtual void AdditionalUpdateAccessibleComponents(RobotCompIDSet& comps) const override {
    comps.insert(RobotComponentID::Vision);
    comps.insert(RobotComponentID::MicComponent);
    comps.insert(RobotComponentID::ProxSensor);
  }

  virtual void UpdateDependent(const RobotCompMap& dependentComps) override;

  void NotifyOfRobotState(const RobotState& robotState);

  // Request the robot enter power save mode. If any requests are active, this component will attempt to enter
  // power save. The requester string should be unique and is useful for debugging
  void RequestPowerSaveMode(const std::string& requester);

  // Remove a specific power save request, and return true if one was found. Once no requests remain, this
  // component will attempt to leave power save mode
  bool RemovePowerSaveModeRequest(const std::string& requester);

  bool IsPowerSaveRequestPending() const { return _powerSaveRequests.empty() == _inPowerSaveMode; }

  bool InPowerSaveMode() const { return _inPowerSaveMode; }

  bool InSysconCalmMode() const { return _inSysconCalmMode; }

  void RequestLCDBrightnessChange(const LCDBrightness& level) const;

  // NOTE: In an ideal system, we'd work the opposite way, where specific behaviors or pieces of code could
  // request a higher power mode, and the _default_ would be power save. This would potentially allow better
  // power saving, but also be harder to debug and have systems understand what to do and not do in power save
  // mode, so for now it's "opt-in" instead of "opt-out"

  // prevent hiding warnings (use the robot component versions for real)
  using UnreliableComponent<BCComponentID>::GetInitDependencies;
  using UnreliableComponent<BCComponentID>::InitDependent;
  using UnreliableComponent<BCComponentID>::GetUpdateDependencies;
  using UnreliableComponent<BCComponentID>::UpdateDependent;
  using UnreliableComponent<BCComponentID>::AdditionalUpdateAccessibleComponents;

private:
  
  std::vector<Signal::SmartHandle> _signalHandles;

  std::multiset<std::string> _powerSaveRequests;
  bool _inPowerSaveMode = false;
  bool _inSysconCalmMode = false;

  float _timePowerSaveToggled_s = -1.0f;

  void EnterPowerSave(const RobotCompMap& components);
  void ExitPowerSave(const RobotCompMap& components);

  void TogglePowerSaveSetting( const RobotCompMap& components, PowerSaveSetting setting, bool enabled );

  const CozmoContext* _context = nullptr;

  std::set<PowerSaveSetting> _enabledSettings;

  enum class CameraState {
    ShouldInit,
    Running,
    ShouldDelete,
    Deleted
  };

  CameraState _cameraState = CameraState::Running;

  bool _cpuThrottleLow = true;
  
  float _nextSendWebVizDataTime_sec = 0.0f;
};

}
}

#endif

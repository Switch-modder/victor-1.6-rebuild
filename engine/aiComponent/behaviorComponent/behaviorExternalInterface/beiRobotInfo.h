/**
* File: beiRobotInfo.h
*
* Author: Kevin M. Karol
* Created: 11/17/17
*
* Description: Wrapper that hides robot from the BEI and only exposes information
* that has been deemed appropriate for the BEI to access
*
* Copyright: Anki, Inc. 2017
*
**/

#ifndef __Cozmo_Basestation_BehaviorSystem_BEIRobotInfo_H__
#define __Cozmo_Basestation_BehaviorSystem_BEIRobotInfo_H__

#include "coretech/common/engine/math/pose.h"
#include "coretech/common/engine/robotTimeStamp.h"
#include "engine/actions/actionContainers.h"
#include "engine/aiComponent/behaviorComponent/behaviorComponents_fwd.h"
#include "engine/engineTimeStamp.h"
#include "util/entityComponent/iDependencyManagedComponent.h"
#include "clad/types/batteryTypes.h"
#include "clad/types/offTreadsStates.h"

// forward declaration
class Quad2f;

namespace Anki {

class PoseOriginList;
namespace Util {
class RandomGenerator;

namespace Data {
class DataPlatform;
}
}

namespace Vector {

// forward declaration
class ActionList;
class BatteryComponent;
class CarryingComponent;
class CliffSensorComponent;
class CozmoContext;
class DockingComponent;
class DrivingAnimationHandler;
class IExternalInterface;
class IGatewayInterface;
class MoodManager;
class MovementComponent;
class NVStorageComponent;
class PathComponent;
class ProgressionUnlockComponent;
class ProxSensorComponent;
class PublicStateBroadcaster;
class Robot;
class RobotEventHandler;
class SDKComponent;
class VisionComponent;
class LocaleComponent;

struct AccelData;
struct GyroData;


class BEIRobotInfo : public IDependencyManagedComponent<BCComponentID> {
public:
  BEIRobotInfo(Robot& robot)
  : IDependencyManagedComponent(this, BCComponentID::RobotInfo)
  , _robot(robot){};
  virtual ~BEIRobotInfo();

  //////
  // IDependencyManagedComponent functions
  //////
  virtual void InitDependent(Robot* robot, const BCCompMap& dependentComps) override {};
  virtual void GetInitDependencies(BCCompIDSet& dependencies) const override {};
  virtual void GetUpdateDependencies(BCCompIDSet& dependencies) const override {};
  //////
  // end IDependencyManagedComponent functions
  //////


  ActionList&                 GetActionList();
  BatteryComponent&           GetBatteryComponent()                   const;
  BatteryLevel                GetBatteryLevel()                       const;
  BatteryLevel                GetPrevBatteryLevel()                   const;
  Quad2f                      GetBoundingQuadXY(const Pose3d& atPose) const;
  CarryingComponent&          GetCarryingComponent()                  const;
  const CliffSensorComponent& GetCliffSensorComponent()               const;
  const CozmoContext*         GetContext()                            const;
  uint32_t                    GetCpuTemperature_degC()                const;
  Util::Data::DataPlatform*   GetDataPlatform()                       const;
  u32                         GetDisplayHeightInPixels()              const;
  u32                         GetDisplayWidthInPixels()               const;
  DockingComponent&           GetDockingComponent()                   const;
  DrivingAnimationHandler&    GetDrivingAnimationHandler()            const;
  const AccelData&            GetHeadAccelData()                      const;
  float                       GetHeadAccelMagnitudeFiltered()         const;
  const f32                   GetHeadAngle()                          const;
  const GyroData&             GetHeadGyroData()                       const;
  u32                         GetHeadSerialNumber()                   const;
  RobotTimeStamp_t            GetLastImageTimeStamp()                 const;
  RobotTimeStamp_t            GetLastMsgTimestamp()                   const;
  f32                         GetLiftAngle()                          const;
  f32                         GetLiftHeight()                         const;
  MovementComponent&          GetMoveComponent()                      const;
  NVStorageComponent&         GetNVStorageComponent()                 const;
  OffTreadsState              GetOffTreadsState()                     const;
  EngineTimeStamp_t           GetOffTreadsStateLastChangedTime_ms()   const;
  PathComponent&              GetPathComponent()                      const;
  Radians                     GetPitchAngle()                         const;
  Radians                     GetRollAngle()                          const;
  const Pose3d&               GetPose()                               const;
  const PoseOriginList&       GetPoseOriginList()                     const;
  const ProxSensorComponent&  GetProxSensorComponent()                const;
  Util::RandomGenerator&      GetRNG();
  RobotEventHandler&          GetRobotEventHandler()                  const;
  SDKComponent&               GetSDKComponent()                       const;
  u32                         GetTimeSinceLastPoke_ms()               const;
  TimeStamp_t                 GetTimeSincePowerButtonPressed_ms()     const;
  const Pose3d&               GetWorldOrigin()                        const;
  PoseOriginID_t              GetWorldOriginID()                      const;
  const LocaleComponent &     GetLocaleComponent()                    const;

  bool HasExternalInterface() const;
  IExternalInterface* GetExternalInterface();

  bool HasGatewayInterface() const;
  IGatewayInterface* GetGatewayInterface();

  Result ComputeHeadAngleToSeePose(const Pose3d& pose, Radians& headAngle, f32 yTolFrac) const;

  bool IsCharging() const;
  float GetTimeAtBatteryLevelSec(BatteryLevel level) const;
  float GetOnChargerDurationSec() const;
  bool IsHeadCalibrated() const;
  bool IsLiftCalibrated() const;
  bool IsHeadMotorOutOfBounds() const;
  bool IsLiftMotorOutOfBounds() const;
  bool IsHeadEncoderInvalid() const;
  bool IsLiftEncoderInvalid() const;
  bool IsOnChargerContacts() const;
  bool IsOnChargerPlatform() const;
  bool IsPhysical() const;
  bool IsPickedUp() const;
  bool IsPowerButtonPressed() const;

  bool IsBeingHeld() const;
  EngineTimeStamp_t GetBeingHeldLastChangedTime_ms() const;

  bool IsPoseInWorldOrigin(const Pose3d& pose) const;

  bool IsCarryingObject() const;

  void EnableStopOnCliff(const bool enable);

private:
  // let the test classes access robot directly
  friend class BehaviorFactoryCentroidExtractor;
  friend class BehaviorFactoryTest;
  friend class BehaviorDevBatteryLogging;
  friend class BehaviorDockingTestSimple;
  friend class BehaviorLiftLoadTest;
  friend class BehaviorDevSquawkBoxTest;

  friend class IBehaviorPlaypen;
  friend class BehaviorPlaypenInitChecks;
  friend class BehaviorPlaypenDriftCheck;
  friend class BehaviorPlaypenPickupCube;
  friend class BehaviorPlaypenMotorCalibration;
  friend class BehaviorPlaypenDistanceSensor;
  friend class BehaviorPlaypenSoundCheck;
  friend class BehaviorPlaypenEndChecks;
  friend class BehaviorPlaypenDriveForwards;
  friend class BehaviorPlaypenTest;
  friend class BehaviorPlaypenWaitToStart;
  friend class BehaviorPlaypenCameraCalibration;

  friend class BehaviorSelfTest;
  friend class BehaviorSelfTestPutOnCharger;
  friend class BehaviorSelfTestScreenAndBackpack;
  friend class BehaviorSelfTestMotorCalibration;
  friend class BehaviorSelfTestDriftCheck;
  friend class BehaviorSelfTestInitChecks;
  friend class BehaviorSelfTestSoundCheck;
  friend class BehaviorSelfTestTouch;
  friend class BehaviorSelfTestButton;
  friend class BehaviorSelfTestDriveForwards;
  friend class BehaviorSelfTestLookAtCharger;
  friend class BehaviorSelfTestDockWithCharger;
  friend class BehaviorSelfTestPickup;

  Robot& _robot;
};

} // namespace Vector
} // namespace Anki

#endif // __Cozmo_Basestation_BehaviorSystem_BEIRobotInfo_H__

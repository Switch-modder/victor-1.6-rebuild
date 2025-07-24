/**
 * File: movementComponent.cpp
 *
 * Author: Lee Crippen
 * Created: 10/21/2015
 *
 * Description: Robot component to handle logic and messages associated with the robot moving.
 *
 * Copyright: Anki, Inc. 2015
 *
 **/

#include "engine/components/movementComponent.h"

#include "coretech/common/robot/utilities.h"
#include "coretech/common/shared/types.h"
#include "coretech/common/engine/math/fastPolygon2d.h"
#include "coretech/common/engine/math/polygon_impl.h"

#include "engine/ankiEventUtil.h"
#include "engine/aiComponent/behaviorComponent/behaviors/iCozmoBehavior.h"
#include "engine/navMap/memoryMap/data/memoryMapData.h"
#include "engine/navMap/mapComponent.h"
#include "engine/components/animationComponent.h"
#include "engine/components/animTrackHelpers.h"
#include "engine/components/battery/batteryComponent.h"
#include "engine/components/dockingComponent.h"
#include "engine/components/robotStatsTracker.h"
#include "engine/cozmoContext.h"
#include "engine/events/ankiEvent.h"
#include "engine/externalInterface/externalInterface.h"
#include "engine/robot.h"
#include "engine/robotManager.h"
#include "engine/robotStateHistory.h"
#include "anki/cozmo/shared/cozmoConfig.h"
#include "clad/externalInterface/messageGameToEngine.h"
#include "clad/robotInterface/messageEngineToRobot.h"
#include "util/console/consoleInterface.h"
#include "util/logging/DAS.h"


#define LOG_CHANNEL    "Movement"

#define DEBUG_ANIMATION_LOCKING 0

namespace Anki {
namespace Vector {
  
CONSOLE_VAR(bool, kDebugTrackLocking, "Robot", false);
CONSOLE_VAR(bool, kCreateUnexpectedMovementObstacles, "Robot", true);
  
using namespace ExternalInterface;


MovementComponent::MovementComponent()
: IDependencyManagedComponent<RobotComponentID>(this, RobotComponentID::Movement)
{

}
 
void MovementComponent::InitDependent(Vector::Robot* robot, const RobotCompMap& dependentComps)
{
  _robot = robot;
  _animComponent = dependentComps.GetComponentPtr<AnimationComponent>();
  if (_robot->HasExternalInterface())
  {
    InitEventHandlers(*(_robot->GetExternalInterface()));
  }
}

  
void MovementComponent::InitEventHandlers(IExternalInterface& interface)
{
  auto helper = MakeAnkiEventUtil(interface, *this, _eventHandles);
  
  // Game to engine (in alphabetical order)
  helper.SubscribeGameToEngine<MessageGameToEngineTag::DriveArc>();
  helper.SubscribeGameToEngine<MessageGameToEngineTag::DriveWheels>();
  helper.SubscribeGameToEngine<MessageGameToEngineTag::MoveHead>();
  helper.SubscribeGameToEngine<MessageGameToEngineTag::MoveLift>();
  helper.SubscribeGameToEngine<MessageGameToEngineTag::StopAllMotors>();
  helper.SubscribeGameToEngine<MessageGameToEngineTag::TurnInPlaceAtSpeed>();
}

  
void MovementComponent::AllowExternalMovementCommands(const bool enable, const std::string& requester)
{
  auto& requesters = _allowExternalMovementCommandNames;
  if (enable) {
    requesters.insert(requester);
    SetAllowExternalMovementCommands(true);
  } else {
    requesters.erase(requester);
    if (requesters.empty()) {
      SetAllowExternalMovementCommands(false);
    }
  }
}


void MovementComponent::SetAllowExternalMovementCommands(const bool enable)
{
  if (enable != _allowExternalMovementCommands) {
    LOG_INFO("MovementComponent.SetAllowExternalMovementCommands.AllowedToHandleActions",
             "Setting _allowExternalMovementCommands to %s",
             enable ? "true" : "false");
    
    _allowExternalMovementCommands = enable;
  }
}

void MovementComponent::OnRobotDelocalized()
{
  _unexpectedMovement.Reset();
}
  
void MovementComponent::NotifyOfRobotState(const Vector::RobotState& robotState)
{
  #define IS_STATUS_FLAG_SET(x) ((robotState.status & (uint32_t)RobotStatusFlag::x) != 0)

  _isMoving            =  IS_STATUS_FLAG_SET(IS_MOVING);
  _isHeadMoving        = !IS_STATUS_FLAG_SET(HEAD_IN_POS);
  _isLiftMoving        = !IS_STATUS_FLAG_SET(LIFT_IN_POS);
  _areWheelsMoving     =  IS_STATUS_FLAG_SET(ARE_WHEELS_MOVING);
  _areEncodersDisabled =  IS_STATUS_FLAG_SET(ENCODERS_DISABLED);

  const bool isHeadEncoderInvalid = IS_STATUS_FLAG_SET(ENCODER_HEAD_INVALID);
  const bool isLiftEncoderInvalid = IS_STATUS_FLAG_SET(ENCODER_LIFT_INVALID);
  
  // Print DAS message when head or lift go out of calibration according to syscon
  if (isHeadEncoderInvalid && !_isHeadEncoderInvalid) {
    DASMSG(head_motor_uncalibrated, "head_motor_uncalibrated", "Head is uncalibrated. (Inverse msg: head_motor_calibrated)");
    DASMSG_SEND();
  }
  if (isLiftEncoderInvalid && !_isLiftEncoderInvalid) {
    DASMSG(lift_motor_uncalibrated, "lift_motor_uncalibrated", "Lift is uncalibrated. (Inverse msg: lift_motor_calibrated)");
    DASMSG_SEND();
  }

  _isHeadEncoderInvalid =  isHeadEncoderInvalid;
  _isLiftEncoderInvalid =  isLiftEncoderInvalid;



  // NOTE(GB): In the future, the meaning of `_isMoving` may change, and may not be coupled to
  // _isHeadMoving, _isLiftMoving, or _areWheelsMoving, so check if we can set each timestamp individually.
  if (_isMoving) {
    _lastTimeWasMoving = robotState.timestamp;
  }
  if (_isHeadMoving) {
    _lastTimeHeadWasMoving = robotState.timestamp;
  }
  if (_isLiftMoving) {
    _lastTimeLiftWasMoving = robotState.timestamp;
  }
  if (_areWheelsMoving) {
    _lastTimeWheelsWereMoving = robotState.timestamp;
  }
  
  for (auto layerIter = _eyeShiftToRemove.begin(); layerIter != _eyeShiftToRemove.end(); )
  {
    EyeShiftToRemove& layer = layerIter->second;
    if(_isHeadMoving && !layer.headWasMoving) {
      // Wait for transition from stopped to moving again
      _robot->GetAnimationComponent().RemoveEyeShift(layerIter->first, layer.duration_ms);
      layerIter = _eyeShiftToRemove.erase(layerIter);
    } else {
      layer.headWasMoving = _isHeadMoving;
      ++layerIter;
    }
  }

  if (kDebugTrackLocking)
  {
    // Flip logic from enabled to locked here, since robot stores bits as enabled and 1 means locked here.
    u8 robotLockedTracks = GetLockedTracks();
    const bool isRobotHeadTrackLocked = (robotLockedTracks & (u8)AnimTrackFlag::HEAD_TRACK) != 0;
    if (isRobotHeadTrackLocked != AreAnyTracksLocked((u8)AnimTrackFlag::HEAD_TRACK))
    {
      LOG_WARNING("MovementComponent.Update.HeadLockUnmatched",
                  "TrackRobot:%d, TrackEngineCount:%zu",
                  ~GetLockedTracks(),
                  _trackLockCount[GetFlagIndex((u8)AnimTrackFlag::HEAD_TRACK)].size());
    }
    const bool isRobotLiftTrackLocked = (robotLockedTracks & (u8)AnimTrackFlag::LIFT_TRACK) != 0;
    if (isRobotLiftTrackLocked != AreAnyTracksLocked((u8)AnimTrackFlag::LIFT_TRACK))
    {
      LOG_WARNING("MovementComponent.Update.LiftLockUnmatched",
                  "TrackRobot:%d, TrackEngineCount:%zu",
                  ~GetLockedTracks(),
                  _trackLockCount[GetFlagIndex((u8)AnimTrackFlag::LIFT_TRACK)].size());
    }
    const bool isRobotBodyTrackLocked = (robotLockedTracks & (u8)AnimTrackFlag::BODY_TRACK) != 0;
    if (isRobotBodyTrackLocked != AreAnyTracksLocked((u8)AnimTrackFlag::BODY_TRACK))
    {
      LOG_WARNING("MovementComponent.Update.BodyLockUnmatched",
                  "TrackRobot:%d, TrackEngineCount:%zu",
                  ~GetLockedTracks(),
                  _trackLockCount[GetFlagIndex((u8)AnimTrackFlag::BODY_TRACK)].size());
    }
  }
  
  UpdateOdometers(robotState);

  CheckForUnexpectedMovement(robotState);
}

void MovementComponent::UpdateOdometers(const Vector::RobotState& robotState)
{
  const float lWheelSpeed_mmps  = robotState.lwheel_speed_mmps;
  const float rWheelSpeed_mmps  = robotState.rwheel_speed_mmps;
  const float bodySpeed_mmps    = 0.5f * (lWheelSpeed_mmps + rWheelSpeed_mmps);

  // Convert speeds into absolute distances
  static const float dt = Util::MilliSecToSec(static_cast<float>(STATE_MESSAGE_FREQUENCY * ROBOT_TIME_STEP_MS));
  const float lWheelDelta_mm = std::fabs(lWheelSpeed_mmps) * dt;
  const float rWheelDelta_mm = std::fabs(rWheelSpeed_mmps) * dt;
  const float bodyDelta_mm   = std::fabs(bodySpeed_mmps)   * dt;

  _lWheel_odom_mm += lWheelDelta_mm;
  _rWheel_odom_mm += rWheelDelta_mm;
  _body_odom_mm   += bodyDelta_mm;

  _robot->GetComponent<RobotStatsTracker>().IncreaseOdometer(lWheelDelta_mm, rWheelDelta_mm, bodyDelta_mm);
}

void MovementComponent::CheckForUnexpectedMovement(const Vector::RobotState& robotState)
{
  const bool wasUnexpectedMovementDetected = _unexpectedMovement.IsDetected();
  
  // Disabling for sim robot due to odd behavior of measured wheel speeds and the lack
  // of wheel slip that is what allows this detection to work on the real robot
  if (!_robot->IsPhysical())
  {
    return;
  }
  
  // Don't check for unexpected movement while playing animations that are moving the body
  // because some of them rapidly move the motors which triggers false positives
  // Or the robot is picking/placing
  if ((_robot->GetAnimationComponent().IsAnimating() && !AreAllTracksLocked((u8)AnimTrackFlag::BODY_TRACK))
      || _robot->GetDockingComponent().IsPickingOrPlacing())
  {
    _unexpectedMovement.Reset();
    return;
  }
  
  const bool isPickedUp = robotState.status & (uint32_t)RobotStatusFlag::IS_PICKED_UP;
  // Don't check for unexpected movement under the following conditions
  if (robotState.status & (uint32_t)RobotStatusFlag::IS_FALLING     ||
      robotState.status & (uint32_t)RobotStatusFlag::IS_ON_CHARGER  ||
      (isPickedUp && !_heldInPalmModeEnabled) )
  {
    _unexpectedMovement.Reset();
    return;
  }
  
  const f32 lWheelSpeed_mmps = robotState.lwheel_speed_mmps;
  const f32 rWheelSpeed_mmps = robotState.rwheel_speed_mmps;
  const f32 zGyro_radps      = robotState.gyro.z;
  
  const f32 speedDiff = rWheelSpeed_mmps - lWheelSpeed_mmps;
  const f32 omega = std::abs(speedDiff / WHEEL_DIST_MM);
  
  UnexpectedMovementType unexpectedMovementType = UnexpectedMovementType::TURNED_BUT_STOPPED;
  
  bool rotatingWithoutMotorsDetected = false;
  const bool noGyroRotation = NEAR(zGyro_radps, 0, kGyroTol_radps);
  // Wheels aren't moving (using kMinWheelSpeed_mmps as a dead band)
  if (ABS(lWheelSpeed_mmps) + ABS(rWheelSpeed_mmps) < kMinWheelSpeed_mmps)
  {
    if (noGyroRotation || !_enableRotatedWithoutMotors)
    {
      _unexpectedMovement.Decrement();
      return;
    }
    else
    {
      // Increment by 5 here to get this case to trigger faster than other cases
      // The intuition is that this case is easier and more reliable to detect so there should not be any false positives
      _unexpectedMovement.Increment(5, lWheelSpeed_mmps, rWheelSpeed_mmps, robotState.timestamp);
      unexpectedMovementType = UnexpectedMovementType::ROTATING_WITHOUT_MOTORS;
      rotatingWithoutMotorsDetected = true;
    }
  }
  
  // Gyro says we aren't turning
  if (noGyroRotation)
  {
    // But wheels say we are turning (the direction of the wheels are different like during a point turn)
    if (signbit(lWheelSpeed_mmps) != signbit(rWheelSpeed_mmps))
    {
      _unexpectedMovement.Increment(1, lWheelSpeed_mmps, rWheelSpeed_mmps, robotState.timestamp);
      unexpectedMovementType = UnexpectedMovementType::TURNED_BUT_STOPPED;
    }
    // Wheels are moving in same direction but the difference between what the gyro is reporting and what we expect
    // it to be reporting is too great
    // The expected rotational velocity, omega, is divided by 2 here due to the fact that the world isn't perfect
    // (lots of wheel slip and friction) so its tends to report velocities 2 times greater than what we are actually
    // measuring
    else if (ABS(ABS(omega*0.5f) - ABS(zGyro_radps)) > kExpectedVsActualGyroTol_radps)
    {
      _unexpectedMovement.Increment(1, lWheelSpeed_mmps, rWheelSpeed_mmps, robotState.timestamp);
      unexpectedMovementType = UnexpectedMovementType::TURNED_BUT_STOPPED;
    }
  }
  // Wheel speeds and gyro agree on the direction we are turning
  else if ((signbit(speedDiff) == signbit(zGyro_radps)) && !rotatingWithoutMotorsDetected)
  {
    // This check is for detecting if we are being turned in the direction we are already turning
    // this is difficult to detect due to physics. For example, we spin faster when carrying a block
    // than not carrying one. The surface we are on will also greatly affect this type of movement detection
    // for these reasons this is not enabled
    // With this commented out the case of being spun in the same direction will trigger TURNED_IN_OPPOSITE_DIRECTION
    // when we pass the target turn angle
//    if(!_robot->IsCarryingObject() &&
//       ABS(speedDiff) > kWheelDifForTurning_mmps &&
//       ABS(ABS(speedDiff) - ABS(zGyro_radps)*100) > 30 * MAX(ABS(speedDiff), 100.f) / 100.f)
//    {
//      _unexpectedMovementCount++;
//      unexpectedMovementType = UnexpectedMovementType::TURNED_IN_SAME_DIRECTION;
//    }
    _unexpectedMovement.Decrement();
    return;
  }
  // Otherwise gyro says we are turning but our non-zero wheel speeds don't agree
  else if (!rotatingWithoutMotorsDetected)
  {
    // Increment by 2 here to get this case to trigger twice as fast as other cases
    // The intuition is that this case is easier and more reliable to detect so there should not be any false positives
    _unexpectedMovement.Increment(2, lWheelSpeed_mmps, rWheelSpeed_mmps, robotState.timestamp);
    unexpectedMovementType = UnexpectedMovementType::TURNED_IN_OPPOSITE_DIRECTION;
  }
  
  if (_unexpectedMovement.IsDetected() &&
      !wasUnexpectedMovementDetected)
  {
    LOG_INFO("MovementComponent.CheckForUnexpectedMovement.Detected",
             "Unexpected movement detected %s",
             EnumToString(unexpectedMovementType));
    
    // Only create obstacles for certain types of unexpected movement, and only when
    // enabled by the console var and when ReactToUnexpectedMovement is enabled
    // as a reactionary behavior. (So if someone turns off the reaction behavior
    // adding of obstacles gets turned off too. This is kind of a hack to avoid
    // needing another message/system for turning this on and off.

    _unexpectedMovementSide = UnexpectedMovementSide::UNKNOWN;
    
    // don't create obstacles for ROTATING_WITHOUT_MOTORS or TURNED_IN_SAME_DIRECTION
    const bool isValidTypeOfUnexpectedMovement = (unexpectedMovementType == UnexpectedMovementType::TURNED_BUT_STOPPED ||
                                                  unexpectedMovementType == UnexpectedMovementType::TURNED_IN_OPPOSITE_DIRECTION);
    
    if (kCreateUnexpectedMovementObstacles && isValidTypeOfUnexpectedMovement)
    {
      // Add obstacle based on when this started and how robot was trying to turn
      // TODO: Broadcast sufficient information to blockworld and do it there?
      f32 avgLeftWheelSpeed_mmps, avgRightWheelSpeed_mmps;
      _unexpectedMovement.GetAvgWheelSpeeds(avgLeftWheelSpeed_mmps, avgRightWheelSpeed_mmps);
      
      const bool leftGoingForward  = FLT_GT(avgLeftWheelSpeed_mmps,  0.f);
      const bool rightGoingForward = FLT_GT(avgRightWheelSpeed_mmps, 0.f);
      
      HistRobotState histState;
      RobotTimeStamp_t historicalTime;
      Result poseResult = _robot->GetStateHistory()->ComputeStateAt(_unexpectedMovement.GetStartTime(), historicalTime,
                                                                 histState, true);
      
      if (RESULT_FAIL_ORIGIN_MISMATCH == poseResult)
      {
        // This particular "failure" of pose history can happen and does not deserve
        // a warning, just an info
        LOG_INFO("MovementComponent.CheckForUnexpectedMovement.PoseHistoryOriginMismatch",
                 "Could not get robot pose at t=%u",
                 (TimeStamp_t)_unexpectedMovement.GetStartTime());
      }
      else if (RESULT_OK != poseResult)
      {
        LOG_WARNING("MovementComponent.CheckForUnexpectedMovement.PoseHistoryFailure",
                    "Could not get robot pose at t=%u",
                    (TimeStamp_t)_unexpectedMovement.GetStartTime());
      }
      else
      {
        Pose3d obstaclePoseWrtRobot;
        
        const f32 obstaclePositionPad_mm = 25.f;
        
        const char *debugStr = "";
        
        if (leftGoingForward != rightGoingForward)
        {
          // Turning
          
          if (leftGoingForward)
          {
            // Put obstacle on right side
            _unexpectedMovementSide = UnexpectedMovementSide::RIGHT;
            obstaclePoseWrtRobot.SetRotation(-M_PI_2_F, Z_AXIS_3D());
            obstaclePoseWrtRobot.SetTranslation({0.f, -0.5f*ROBOT_BOUNDING_Y - obstaclePositionPad_mm, 0.f});
            debugStr = "to right of";
          }
          else
          {
            // Put obstacle on left side
            _unexpectedMovementSide = UnexpectedMovementSide::LEFT;
            obstaclePoseWrtRobot.SetRotation(M_PI_2_F, Z_AXIS_3D());
            obstaclePoseWrtRobot.SetTranslation({0.f, 0.5f*ROBOT_BOUNDING_Y + obstaclePositionPad_mm, 0.f});
            debugStr = "to left of";
          }
        }
        else
        {
          // Going (roughly) forward or backward
          
          if (leftGoingForward && rightGoingForward)
          {
            // Put obstacle in front of robot (note: leave unrotated)
            _unexpectedMovementSide = UnexpectedMovementSide::FRONT;
            obstaclePoseWrtRobot.SetTranslation({ROBOT_BOUNDING_X_FRONT + obstaclePositionPad_mm, 0.f, 0.f});
            debugStr = "in front of";
          }
          else
          {
            // Put obstacle behind robot
            _unexpectedMovementSide = UnexpectedMovementSide::BACK;
            obstaclePoseWrtRobot.SetTranslation({ROBOT_BOUNDING_X_FRONT - ROBOT_BOUNDING_X - obstaclePositionPad_mm, 0.f, 0.f});
            debugStr = "behind";
          }
        }
        
        // Put the robot's _position_ back where it was when the unexpected movement began.
        // However, use the latest _orientation_. (Gyro keeps updating if the robot is stuck,
        // but we were likely slipping, so translation is garbage.)
        // Note that this also increments pose frame ID and sends a localization update
        // to the physical robot.
        Pose3d newPose(histState.GetPose());
        newPose.SetRotation(_robot->GetPose().GetRotation());
        Result res = _robot->SetNewPose(newPose);
        if (res != RESULT_OK)
        {
          LOG_WARNING("MovementComponent.CheckForUnexpectedMovement.SetNewPose", "Failed to set new pose");
        }
        
        // Create obstacle relative to robot at its new pose
        obstaclePoseWrtRobot.SetParent(_robot->GetPose());
        const Pose3d& obstaclePose = obstaclePoseWrtRobot.GetWithRespectToRoot();

        LOG_INFO("MovementComponent.CheckForUnexpectedMovement.AddingCollisionObstacle",
                 "Adding obstacle %s robot",
                 debugStr);
        
        //        p1-p4
        //        |   |         NOTE: add depth after the the detected pose otherwise the
        //      pose  |               robot footprint will override part of the obstacle
        //        |   |               with `ClearOfCliff`
        //        p2-p3

        const Rotation3d id = Rotation3d(0.f, Z_AXIS_3D());
        const Point2f p1 = (obstaclePose.GetTransform() * Transform3d(id, {  0.f, -15.f, 0.f })).GetTranslation();
        const Point2f p2 = (obstaclePose.GetTransform() * Transform3d(id, {  0.f,  15.f, 0.f })).GetTranslation(); 
        const Point2f p3 = (obstaclePose.GetTransform() * Transform3d(id, { 10.f,  15.f, 0.f })).GetTranslation();
        const Point2f p4 = (obstaclePose.GetTransform() * Transform3d(id, { 10.f, -15.f, 0.f })).GetTranslation();

        MemoryMapData data(MemoryMapTypes::EContentType::ObstacleUnrecognized, robotState.timestamp);
        _robot->GetMapComponent().InsertData( FastPolygon({p1, p2, p3, p4}), data);
      }
      
    } // if(kCreateUnexpectedMovementObstacles && isValidTypeOfUnexpectedMovement)
    
    {
      // Broadcast the associated event
      ExternalInterface::UnexpectedMovement msg(robotState.timestamp, unexpectedMovementType, _unexpectedMovementSide);
      _robot->Broadcast(ExternalInterface::MessageEngineToGame(std::move(msg)));
    }
  }
}

void MovementComponent::RemoveEyeShiftWhenHeadMoves(const std::string& name, TimeStamp_t duration_ms)
{
  LOG_DEBUG("MovementComponent.RemoveEyeShiftWhenHeadMoves",
            "Layer: %s, duration=%dms", name.c_str(), duration_ms);

  _eyeShiftToRemove[name].duration_ms  = duration_ms;
  _eyeShiftToRemove[name].headWasMoving = _isHeadMoving;
}
  
void MovementComponent::EnableHeldInPalmMode(const bool enabled)
{
  _heldInPalmModeEnabled = enabled;
  _unexpectedMovement.EnableHeldInPalmMode(enabled);
}

template<>
void MovementComponent::HandleMessage(const ExternalInterface::DriveWheels& msg)
{
  if (!_allowExternalMovementCommands) {
    LOG_WARNING("MovementComponent.EventHandler.DriveWheels.ExternalMovementCommandsNotAllowed",
              "Ignoring ExternalInterface::DriveWheels since external motor commands are not allowed.");
  }
  else if (!_drivingWheels && AreAnyTracksLocked((u8)AnimTrackFlag::BODY_TRACK)) {
    LOG_INFO("MovementComponent.EventHandler.DriveWheels.WheelsLocked",
             "Ignoring ExternalInterface::DriveWheels while wheels are locked.");
  } else {
    DirectDriveCheckSpeedAndLockTracks(ABS(msg.lwheel_speed_mmps) + ABS(msg.rwheel_speed_mmps),
                                       _drivingWheels,
                                       (u8)AnimTrackFlag::BODY_TRACK,
                                       kDrivingWheelsStr,
                                       kDrivingWheelsStr);
    _robot->SendRobotMessage<RobotInterface::DriveWheels>(msg.lwheel_speed_mmps, msg.rwheel_speed_mmps,
                                                         msg.lwheel_accel_mmps2, msg.rwheel_accel_mmps2);
  }
}

template<>
void MovementComponent::HandleMessage(const ExternalInterface::TurnInPlaceAtSpeed& msg)
{
  if (!_allowExternalMovementCommands) {
    LOG_WARNING("MovementComponent.EventHandler.TurnInPlaceAtSpeed.ExternalMovementCommandsNotAllowed",
              "Ignoring ExternalInterface::TurnInPlaceAtSpeed since external motor commands are not allowed.");
  }
  else if (!_drivingWheels && AreAnyTracksLocked((u8)AnimTrackFlag::BODY_TRACK)) {
    LOG_INFO("MovementComponent.EventHandler.TurnInPlaceAtSpeed.WheelsLocked",
             "Ignoring ExternalInterface::TurnInPlaceAtSpeed while wheels are locked.");
  } else {
    f32 turnSpeed = msg.speed_rad_per_sec;
    if (std::fabsf(turnSpeed) > MAX_BODY_ROTATION_SPEED_RAD_PER_SEC) {
      LOG_WARNING("MovementComponent.EventHandler.TurnInPlaceAtSpeed.SpeedExceedsLimit",
                  "Speed of %f deg/s exceeds limit of %f deg/s. Clamping.",
                  RAD_TO_DEG(turnSpeed), MAX_BODY_ROTATION_SPEED_DEG_PER_SEC);
      turnSpeed = std::copysign(MAX_BODY_ROTATION_SPEED_RAD_PER_SEC, turnSpeed);
    }
    DirectDriveCheckSpeedAndLockTracks(turnSpeed,
                                       _drivingWheels,
                                       (u8)AnimTrackFlag::BODY_TRACK,
                                       kDrivingWheelsStr,
                                       kDrivingTurnStr);
    _robot->SendRobotMessage<RobotInterface::TurnInPlaceAtSpeed>(turnSpeed, msg.accel_rad_per_sec2);
  }
}

template<>
void MovementComponent::HandleMessage(const ExternalInterface::MoveHead& msg)
{
  if (!_allowExternalMovementCommands) {
    LOG_WARNING("MovementComponent.EventHandler.MoveHead.ExternalMovementCommandsNotAllowed",
              "Ignoring ExternalInterface::MoveHead since external motor commands are not allowed.");
  }
  else if (!_drivingHead && AreAnyTracksLocked((u8)AnimTrackFlag::HEAD_TRACK)) {
    LOG_INFO("MovementComponent.EventHandler.MoveHead.HeadLocked",
             "Ignoring ExternalInterface::MoveHead while head is locked.");
  } else {
    DirectDriveCheckSpeedAndLockTracks(msg.speed_rad_per_sec,
                                       _drivingHead,
                                       (u8)AnimTrackFlag::HEAD_TRACK,
                                       kDrivingHeadStr,
                                       kDrivingHeadStr);
    _robot->SendRobotMessage<RobotInterface::MoveHead>(msg.speed_rad_per_sec);
  }
}

template<>
void MovementComponent::HandleMessage(const ExternalInterface::MoveLift& msg)
{
  if (!_allowExternalMovementCommands) {
    LOG_WARNING("MovementComponent.EventHandler.MoveLift.ExternalMovementCommandsNotAllowed",
              "Ignoring ExternalInterface::MoveLift since external motor commands are not allowed.");
  }
  else if (!_drivingLift && AreAnyTracksLocked((u8)AnimTrackFlag::LIFT_TRACK)) {
    LOG_INFO("MovementComponent.EventHandler.MoveLift.LiftLocked",
             "Ignoring ExternalInterface::MoveLift while lift is locked.");
  } else {
    DirectDriveCheckSpeedAndLockTracks(msg.speed_rad_per_sec,
                                       _drivingLift,
                                       (u8)AnimTrackFlag::LIFT_TRACK,
                                       kDrivingLiftStr,
                                       kDrivingLiftStr);
    _robot->SendRobotMessage<RobotInterface::MoveLift>(msg.speed_rad_per_sec);
  }
}

template<>
void MovementComponent::HandleMessage(const ExternalInterface::DriveArc& msg)
{
  if (!_allowExternalMovementCommands) {
    LOG_WARNING("MovementComponent.EventHandler.DriveArc.ExternalMovementCommandsNotAllowed",
              "Ignoring ExternalInterface::DriveArc since external motor commands are not allowed.");
  }
  else if (!_drivingWheels && AreAnyTracksLocked((u8)AnimTrackFlag::BODY_TRACK)) {
    LOG_INFO("MovementComponent.EventHandler.DriveArc.WheelsLocked",
             "Ignoring ExternalInterface::DriveArc while wheels are locked.");
  } else {
    DirectDriveCheckSpeedAndLockTracks(msg.speed,
                                       _drivingWheels,
                                       (u8)AnimTrackFlag::BODY_TRACK,
                                       kDrivingWheelsStr,
                                       kDrivingArcStr);
    _robot->SendRobotMessage<RobotInterface::DriveWheelsCurvature>(msg.speed,
                                                                  std::fabsf(msg.accel),
                                                                  msg.curvatureRadius_mm);
  }
}

template<>
void MovementComponent::HandleMessage(const ExternalInterface::StopAllMotors& msg)
{
  if (!_allowExternalMovementCommands) {
    LOG_WARNING("MovementComponent.EventHandler.StopAllMotors.ExternalMovementCommandsNotAllowed",
              "Ignoring ExternalInterface::StopAllMotors since external motor commands are not allowed.");
    return;
  }

  StopAllMotors();
}

void MovementComponent::DirectDriveCheckSpeedAndLockTracks(f32 speed, bool& flag, u8 tracks,
                                                           const std::string& who,
                                                           const std::string& debugName)
{
  if (NEAR_ZERO(speed))
  {
    flag = false;
    if (AreAllTracksLocked(tracks))
    {
      const bool locksLeft = UnlockTracks(tracks, who);
      
      if (locksLeft)
      {
        LOG_ERROR("MovementComponent.DirectDriveCheckSpeedAndLockTracks",
                  "Locks left on tracks %s [0x%x] after %s[%s] unlocked",
                  AnimTrackHelpers::AnimTrackFlagsToString(tracks).c_str(),
                  tracks,
                  debugName.c_str(),
                  who.c_str());
      }
    }
  }
  else
  {
    flag = true;
    if (!AreAllTracksLocked(tracks))
    {
      LockTracks(tracks, who, debugName);
    }
  }
}
  
// =========== Motor commands ============

Result MovementComponent::CalibrateMotors(bool head, bool lift, const MotorCalibrationReason& reason)
{
  return _robot->SendRobotMessage<RobotInterface::StartMotorCalibration>(head, lift, reason);
}
  

Result MovementComponent::EnableLiftPower(bool enable)
{
  return _robot->SendRobotMessage<RobotInterface::EnableMotorPower>(MotorID::MOTOR_LIFT, enable);
}

Result MovementComponent::EnableHeadPower(bool enable)
{
  return _robot->SendRobotMessage<RobotInterface::EnableMotorPower>(MotorID::MOTOR_HEAD, enable);
}
  
MovementComponent::MotorActionID MovementComponent::GetNextMotorActionID(MotorActionID* actionID_out)
{
  // Increment actionID
  // 0 is reserved as it means the robot process should not ack it
  if (++_lastMotorActionID == 0) {
    ++_lastMotorActionID;
  }

  if (actionID_out != nullptr) {
    *actionID_out = _lastMotorActionID;
  }
  return _lastMotorActionID;
}

u8 MovementComponent::GetTracksLockedAtRelativeStreamTime(const TimeStamp_t relativeStreamTime_ms) const
{
  // NOTE: This implementation assumes that no tracks are locked during the time that there is a delayed track message
  // to lock as well. If this assumption is violated delayed messages may be sent that override track locking already sent
  // To handle this properly would require a large number of contract re-writes between the animation process and movement
  // component API, so for now just don't lock tracks after queueing delayed track locks
  u8 trackState = GetLockedTracks();
  std::multimap<std::string, int> lockCountMap;

  // Lock tracks
  for(const auto& entry: _delayedTracksToLock){
    if(entry.first <= relativeStreamTime_ms){
      trackState |= entry.second.tracksToLock;
      auto iter = lockCountMap.find(entry.second.who);
      if(iter == lockCountMap.end()){
        lockCountMap.emplace(entry.second.who, 1);
      }else{
        iter->second++;
      }
    }else{
      break;
    }
  }

  // unlock, checking count
  for(const auto& entry: _delayedTracksToUnlock){
    if(entry.first <= relativeStreamTime_ms){
      auto iter = lockCountMap.find(entry.second.who);
      if(iter == lockCountMap.end()){
        PRINT_NAMED_ERROR("MovementComponent.GetTracksLockedAtRelativeStreamTime.IllegalUnlock",
                          "%s is attempting to unlock a track that is not locked",
                          entry.second.who.c_str());
      }else{
        iter->second--;
        if(iter->second == 0){
          trackState &= ~entry.second.tracksToLock;
        }
      }
    }else{
      break;
    }
  }

  return trackState;
}

// Sends a message to the robot to move the lift to the specified angle
Result MovementComponent::MoveLiftToAngle(const f32 angle_rad,
                                          const f32 max_speed_rad_per_sec,
                                          const f32 accel_rad_per_sec2,
                                          const f32 duration_sec,
                                          MotorActionID* actionID_out)
{
  return _robot->SendRobotMessage<RobotInterface::SetLiftAngle>(angle_rad,
                                                                max_speed_rad_per_sec,
                                                                accel_rad_per_sec2,
                                                                duration_sec,
                                                                GetNextMotorActionID(actionID_out));
}

// Sends a message to the robot to move the lift to the specified height
Result MovementComponent::MoveLiftToHeight(const f32 height_mm,
                                           const f32 max_speed_rad_per_sec,
                                           const f32 accel_rad_per_sec2,
                                           const f32 duration_sec,
                                           MotorActionID* actionID_out)
{
  return _robot->SendRobotMessage<RobotInterface::SetLiftHeight>(height_mm,
                                                                max_speed_rad_per_sec,
                                                                accel_rad_per_sec2,
                                                                duration_sec,
                                                                GetNextMotorActionID(actionID_out));
}

// Sends a message to the robot to move the head to the specified angle
Result MovementComponent::MoveHeadToAngle(const f32 angle_rad,
                                          const f32 max_speed_rad_per_sec,
                                          const f32 accel_rad_per_sec2,
                                          const f32 duration_sec,
                                          MotorActionID* actionID_out)
{
  return _robot->SendRobotMessage<RobotInterface::SetHeadAngle>(angle_rad,
                                                               max_speed_rad_per_sec,
                                                               accel_rad_per_sec2,
                                                               duration_sec,
                                                               GetNextMotorActionID(actionID_out));
}

Result MovementComponent::DriveWheels(const f32 lWheel_speed_mmps,
                                      const f32 rWheel_speed_mmps,
                                      const f32 lWheel_accel_mmps2,
                                      const f32 rWheel_accel_mmps2)
{
  return _robot->SendRobotMessage<RobotInterface::DriveWheels>(lWheel_speed_mmps,
                                                               rWheel_speed_mmps,
                                                               lWheel_accel_mmps2,
                                                               rWheel_accel_mmps2);

}
  
Result MovementComponent::TurnInPlace(const f32 angle_rad,
                                      const f32 max_speed_rad_per_sec,
                                      const f32 accel_rad_per_sec2,
                                      const f32 angle_tolerance,
                                      const u16 num_half_revolutions,
                                      bool use_shortest_direction,
                                      MotorActionID* actionID_out)
{
  f32 max_speed_capped_radps = max_speed_rad_per_sec;
  if (_heldInPalmModeEnabled && max_speed_rad_per_sec > kMaxHeldInPalmTurnSpeed_radps) {
    LOG_INFO("MovementComponent.TurnInPlace.CappingSpeed",
              "Point-turn with max turn speed of %.1f[rad/s] requested, "
              "but capping max speed to %.1f [rad/s] instead, HeldInPalmMode enabled.",
              max_speed_rad_per_sec, kMaxHeldInPalmTurnSpeed_radps);
    max_speed_capped_radps = kMaxHeldInPalmTurnSpeed_radps;
  }
  return _robot->SendRobotMessage<RobotInterface::SetBodyAngle>(angle_rad,
                                                                max_speed_capped_radps,
                                                                accel_rad_per_sec2,
                                                                angle_tolerance,
                                                                num_half_revolutions,
                                                                use_shortest_direction,
                                                                GetNextMotorActionID(actionID_out));
}

Result MovementComponent::StopAllMotors()
{
  // If we are direct driving then make sure to unlock tracks and set flags appropriately
  if (IsDirectDriving())
  {
    DirectDriveCheckSpeedAndLockTracks(0, _drivingHead,   (u8)AnimTrackFlag::HEAD_TRACK, kDrivingHeadStr,   kDrivingHeadStr);
    DirectDriveCheckSpeedAndLockTracks(0, _drivingLift,   (u8)AnimTrackFlag::LIFT_TRACK, kDrivingLiftStr,   kDrivingLiftStr);
    DirectDriveCheckSpeedAndLockTracks(0, _drivingWheels, (u8)AnimTrackFlag::BODY_TRACK, kDrivingWheelsStr, kDrivingWheelsStr);
    DirectDriveCheckSpeedAndLockTracks(0, _drivingWheels, (u8)AnimTrackFlag::BODY_TRACK, kDrivingWheelsStr, kDrivingArcStr);
    DirectDriveCheckSpeedAndLockTracks(0, _drivingWheels, (u8)AnimTrackFlag::BODY_TRACK, kDrivingWheelsStr, kDrivingTurnStr);
  }
  
  return _robot->SendRobotMessage<RobotInterface::StopAllMotors>();
}

Result MovementComponent::StopHead()
{
  // If we are direct driving then make sure to unlock tracks and set flags appropriately
  if (IsDirectDriving())
  {
    DirectDriveCheckSpeedAndLockTracks(0, _drivingHead,   (u8)AnimTrackFlag::HEAD_TRACK, kDrivingHeadStr,   kDrivingHeadStr);
  }
  return _robot->SendRobotMessage<RobotInterface::MoveHead>(RobotInterface::MoveHead(0.f));
}
  
Result MovementComponent::StopLift()
{
  // If we are direct driving then make sure to unlock tracks and set flags appropriately
  if (IsDirectDriving())
  {
    DirectDriveCheckSpeedAndLockTracks(0, _drivingLift,   (u8)AnimTrackFlag::LIFT_TRACK, kDrivingLiftStr,   kDrivingLiftStr);
  }
  return _robot->SendRobotMessage<RobotInterface::MoveLift>(RobotInterface::MoveLift(0.f));
}
  
Result MovementComponent::StopBody()
{
  // If we are direct driving then make sure to unlock tracks and set flags appropriately
  if (IsDirectDriving())
  {
    DirectDriveCheckSpeedAndLockTracks(0, _drivingWheels, (u8)AnimTrackFlag::BODY_TRACK, kDrivingWheelsStr, kDrivingWheelsStr);
    DirectDriveCheckSpeedAndLockTracks(0, _drivingWheels, (u8)AnimTrackFlag::BODY_TRACK, kDrivingWheelsStr, kDrivingArcStr);
    DirectDriveCheckSpeedAndLockTracks(0, _drivingWheels, (u8)AnimTrackFlag::BODY_TRACK, kDrivingWheelsStr, kDrivingTurnStr);
  }
  return _robot->SendRobotMessage<RobotInterface::DriveWheels>(RobotInterface::DriveWheels(0.f,0.f,0.f,0.f));
}
  
int MovementComponent::GetFlagIndex(uint8_t flag) const
{
  int i = 0;
  while (flag > 1)
  {
    i++;
    flag = flag >> 1;
  }
  return i;
}

AnimTrackFlag MovementComponent::GetFlagFromIndex(int index)
{
  return (AnimTrackFlag)((u32)1 << index);
}


uint8_t MovementComponent::GetTracksLockedBy(const std::string& who) const
{
  uint8_t lockedTracks = 0;
  for (int i=0; i < (int)AnimConstants::NUM_TRACKS; i++)
  {
    auto iter = _trackLockCount[i].find({who, ""});
    if (iter != _trackLockCount[i].end())
    {
      lockedTracks |= (1 << i);
    }
  }
  return lockedTracks;
}

uint8_t MovementComponent::GetLockedTracks() const 
{
  uint8_t lockedTracks = 0;
  for (int i=0; i < (int)AnimConstants::NUM_TRACKS; i++)
  {
    if (!_trackLockCount[i].empty())
    {
      lockedTracks |= (1 << i);
    }
  }
  return lockedTracks;
}


bool MovementComponent::AreAnyTracksLocked(u8 tracks) const
{
  for (int i = 0; i < (int)AnimConstants::NUM_TRACKS; i++)
  {
    if ((tracks & 1) && (!_trackLockCount[i].empty()))
    {
      return true;
    }
    tracks = tracks >> 1;
  }
  return false;
}

bool MovementComponent::AreAllTracksLocked(u8 tracks) const
{
  if (tracks == 0)
  {
    LOG_WARNING("MovementComponent.AreAllTracksLocked", "All of the NO_TRACKS are not locked");
    return false;
  }
  for (int i = 0; i < (int)AnimConstants::NUM_TRACKS; i++)
  {
    if ((tracks & 1) && (_trackLockCount[i].size() == 0))
    {
      return false;
    }
    tracks = tracks >> 1;
  }
  return true;
}

bool MovementComponent::AreAllTracksLockedBy(u8 tracks, const std::string& who) const
{
  if (tracks == 0)
  {
    LOG_WARNING("MovementComponent.AreAllTracksLockedBy", "All of the NO_TRACKS are not locked");
    return false;
  }
  for (int i = 0; i < (int)AnimConstants::NUM_TRACKS; i++)
  {
    // If 'who' is not locking this track
    if ((tracks & 1) && (_trackLockCount[i].find({who, ""}) == _trackLockCount[i].end()))
    {
      return false;
    }
    tracks = tracks >> 1;
  }
  return true;
}

void MovementComponent::UnlockAllTracks()
{
  for (int i = 0; i < (int)AnimConstants::NUM_TRACKS; i++)
  {
    if (!_trackLockCount[i].empty())
    {
      LOG_INFO("MovementComponent.UnlockAllTracks",
               "Unlocking track %s",
               EnumToString(GetFlagFromIndex(i)));
      _trackLockCount[i].clear();
    }
  }

  _robot->SendRobotMessage<RobotInterface::SetFullAnimTrackLockState>(GetLockedTracks());
}

void MovementComponent::LockTracks(uint8_t tracks, const std::string& who, const std::string& debugName)
{
  uint8_t tracksToDisable = 0;
  for (int i=0; i < (int)AnimConstants::NUM_TRACKS; i++)
  {
    uint8_t curTrack = (1 << i);
    if ((tracks & curTrack) == curTrack)
    {
      _trackLockCount[i].insert({who, debugName});
      
      // If we just went from not locked to locked, inform the robot
      if (_trackLockCount[i].size() == 1)
      {
        tracksToDisable |= curTrack;
      }
    }
  }
  
  if (tracksToDisable > 0)
  {
    _robot->SendRobotMessage<RobotInterface::SetFullAnimTrackLockState>(GetLockedTracks());
  }
  
  if (DEBUG_ANIMATION_LOCKING) {
    LOG_INFO("MovementComponent.LockTracks", "locked: (0x%x) %s by %s[%s], result:",
             tracks,
             AnimTrackHelpers::AnimTrackFlagsToString(tracks).c_str(),
             debugName.c_str(),
             who.c_str());
    PrintLockState();
  }
}

bool MovementComponent::UnlockTracks(uint8_t tracks, const std::string& who)
{
  bool locksLeft = false;
  uint8_t tracksToEnable = 0;
  for (int i=0; i < (int)AnimConstants::NUM_TRACKS; i++)
  {
    uint8_t curTrack = (1 << i);
    if ((tracks & curTrack) == curTrack)
    {
      auto iter = _trackLockCount[i].find({who, ""});
      if (iter != _trackLockCount[i].end())
      {
        _trackLockCount[i].erase(iter);
        locksLeft |= !_trackLockCount[i].empty();
      }
      else
      {
        LOG_INFO("MovementComponent.UnlockTracks",
                 "Tracks 0x%x are not currently locked by %s",
                  tracks,
                  who.c_str());
        PrintLockState();
      }

      // If we just went from locked to not locked, inform the robot
      if (_trackLockCount[i].size() == 0)
      {
        tracksToEnable |= curTrack;
      }
    }
  }
  
  if (tracksToEnable > 0)
  {
    _robot->SendRobotMessage<RobotInterface::SetFullAnimTrackLockState>(GetLockedTracks());
  }
  
  if (DEBUG_ANIMATION_LOCKING) {
    LOG_INFO("MovementComponent.UnlockTracks", "unlocked: (0x%x) %s by %s, result:",
              tracks,
             AnimTrackHelpers::AnimTrackFlagsToString(tracks).c_str(),
             who.c_str());
    PrintLockState();
  }
  return locksLeft;
}


void MovementComponent::ApplyUpdatesForStreamTime(const TimeStamp_t relativeStreamTime_ms, const bool shouldClearUnusedLocks)
{
  if(!_delayedTracksToLock.empty()){
    auto delayedIter = _delayedTracksToLock.begin();
    while(delayedIter != _delayedTracksToLock.end()){
      if(delayedIter->first <= relativeStreamTime_ms){
        LockTracks(delayedIter->second.tracksToLock, delayedIter->second.who, delayedIter->second.who);
        delayedIter = _delayedTracksToLock.erase(delayedIter);
      }else{
        break;
      }
    }
  }


  if(!_delayedTracksToUnlock.empty()){
    auto delayedIter = _delayedTracksToUnlock.begin();
    while(delayedIter != _delayedTracksToUnlock.end()){
      if(delayedIter->first <= relativeStreamTime_ms){
        UnlockTracks(delayedIter->second.tracksToLock, delayedIter->second.who);
        delayedIter = _delayedTracksToUnlock.erase(delayedIter);
      }else{
        break;
      }
    }
  }

  if(shouldClearUnusedLocks){
    _delayedTracksToLock.clear();
    _delayedTracksToUnlock.clear();
  }

}


void MovementComponent::LockTracksAtStreamTime(const u8 tracks, const TimeStamp_t relativeStreamTime_ms, 
                                               const bool applyBeforeTick, 
                                               const std::string& who, const std::string& debugName)
{
  // Figure out what the track state will be at the given time so that the full anim track lock set is correct
  _delayedTracksToLock.emplace(relativeStreamTime_ms, DelayedTrackLockInfo(tracks, who));
  u8 lockedAtStreamTime = GetTracksLockedAtRelativeStreamTime(relativeStreamTime_ms);
  lockedAtStreamTime |= tracks;

  RobotInterface::SetFullAnimTrackLockState msg(lockedAtStreamTime);
  RobotInterface::EngineToRobot wrapper(std::move(msg));
  _animComponent->AlterStreamingAnimationAtTime(std::move(wrapper), 
                                                relativeStreamTime_ms,
                                                applyBeforeTick,
                                                this);
}


void MovementComponent::UnlockTracksAtStreamTime(const u8 tracks, const TimeStamp_t relativeStreamTime_ms, 
                                                 const bool applyBeforeTick, const std::string& who)
{
  // Figure out what the track state will be at the given time so that the full anim track lock set is correct
  _delayedTracksToUnlock.emplace(relativeStreamTime_ms, DelayedTrackLockInfo(tracks, who));
  u8 lockedAtStreamTime = GetTracksLockedAtRelativeStreamTime(relativeStreamTime_ms);
  lockedAtStreamTime &= ~tracks;

  RobotInterface::SetFullAnimTrackLockState msg(lockedAtStreamTime);
  RobotInterface::EngineToRobot wrapper(std::move(msg));
  _animComponent->AlterStreamingAnimationAtTime(std::move(wrapper), 
                                                relativeStreamTime_ms,
                                                applyBeforeTick,
                                                this);
}

void MovementComponent::PrintLockState() const
{
  std::stringstream ss;
  for (int trackNum = 0; trackNum < (int)AnimConstants::NUM_TRACKS; ++trackNum) {
    if (!_trackLockCount[trackNum].empty()) {
      uint8_t trackEnumVal = 1 << trackNum;
      ss << AnimTrackHelpers::AnimTrackFlagsToString(trackEnumVal) << ":" << _trackLockCount[trackNum].size() << ' ';
      for (auto iter : _trackLockCount[trackNum])
      {
        ss << iter.debugName << "[" << iter.who << "] ";
      }
      ss << '\n';
    }
  }

  LOG_INFO("MovementComponent.LockState", "%s", ss.str().c_str());
}

std::string MovementComponent::WhoIsLocking(u8 trackFlags) const
{
  std::stringstream ss;
  for (int i = 0; i < (int)AnimConstants::NUM_TRACKS; i++)
  {
    if ((trackFlags & 1) && (!_trackLockCount[i].empty()))
    {
      const u8 trackFlag = (1 << i);
      ss << AnimTrackHelpers::AnimTrackFlagsToString(trackFlag);
      ss << " locked by ";
      for (const auto& lockInfo : _trackLockCount[i])
      {
        ss << lockInfo.debugName << "[" << lockInfo.who << "] ";
      }
      ss << "  ";
    }
    trackFlags = trackFlags >> 1;
  }
  return ss.str();
}
  
RobotTimeStamp_t MovementComponent::GetLastTimeCameraWasMoving() const
{
  return std::max(_lastTimeWheelsWereMoving, _lastTimeHeadWasMoving);
}

  static inline bool WasMovingHelper(Robot& robot, RobotTimeStamp_t atTime, const std::string& debugStr,
                                     std::function<bool(const HistRobotState&)>&& wasMovingFcn)
  {
    bool wasMoving = false;
    
    RobotTimeStamp_t t_hist = 0;
    HistRobotState histState;
    const bool kWithInterpolation = true;
    const Result result = robot.GetStateHistory()->GetRawStateAt(atTime, t_hist, histState, kWithInterpolation);
    if (RESULT_OK != result)
    {
      LOG_WARNING(("MovementComponent." + debugStr + ".GetHistoricalPoseFailed").c_str(),
                  "t=%u", (TimeStamp_t)atTime);
      wasMoving = true;
    }
    else
    {
      wasMoving = wasMovingFcn(histState);
    }
    
    return wasMoving;
  }
  
  bool MovementComponent::WasMoving(RobotTimeStamp_t atTime)
  {
    const bool wasMoving = WasMovingHelper(*_robot, atTime, "WasMoving",
                                           [](const HistRobotState& s) { return s.WasMoving(); });
    return wasMoving;
  }
  
  bool MovementComponent::WasHeadMoving(RobotTimeStamp_t atTime)
  {
    const bool wasHeadMoving = WasMovingHelper(*_robot, atTime, "WasHeadMoving",
                                               [](const HistRobotState& s) { return s.WasHeadMoving(); });
    return wasHeadMoving;
  }
  
  bool MovementComponent::WasLiftMoving(RobotTimeStamp_t atTime)
  {
    const bool wasLiftMoving = WasMovingHelper(*_robot, atTime, "WasLiftMoving",
                                               [](const HistRobotState& s) { return s.WasLiftMoving(); });
    return wasLiftMoving;
  }
  
  bool MovementComponent::WereWheelsMoving(RobotTimeStamp_t atTime)
  {
    const bool wereWheelsMoving = WasMovingHelper(*_robot, atTime, "WereWheelsMoving",
                                                  [](const HistRobotState& s) { return s.WereWheelsMoving(); });
    return wereWheelsMoving;
  }
  
  bool MovementComponent::WasCameraMoving(RobotTimeStamp_t atTime)
  {
    const bool wasCameraMoving = WasMovingHelper(*_robot, atTime, "WasCameraMoving",
                                                 [](const HistRobotState& s) { return s.WasCameraMoving(); });
    return wasCameraMoving;
  }
  
#pragma mark -
#pragma mark Unexpected Movement

const u8 MovementComponent::UnexpectedMovement::kMaxUnexpectedMovementCount = 11;
CONSOLE_VAR(u8, kMaxUnexpectedMovementCountWhileHeldInPalm, "Robot", 200);

bool MovementComponent::UnexpectedMovement::IsDetected() const
{
  return _count >= GetMaxCount();
}

void MovementComponent::UnexpectedMovement::Increment(u8 countInc, f32 leftSpeed_mmps, f32 rightSpeed_mmps, RobotTimeStamp_t currentTime)
{
  if (_count == 0)
  {
    _startTime = currentTime;
  }
  _count += countInc;
  _count = std::min(_count, GetMaxCount());
  _sumWheelSpeedL_mmps += (f32)countInc * leftSpeed_mmps;
  _sumWheelSpeedR_mmps += (f32)countInc * rightSpeed_mmps;
}

void MovementComponent::UnexpectedMovement::Decrement()
{
  if (_count > 0)
  {
    --_count;
  }
}

void MovementComponent::UnexpectedMovement::Reset()
{
  _startTime = 0;
  _count = 0;
  _sumWheelSpeedL_mmps = 0.f;
  _sumWheelSpeedR_mmps = 0.f;
}
  
void MovementComponent::UnexpectedMovement::GetAvgWheelSpeeds(f32& left, f32& right) const
{
  left  = _sumWheelSpeedL_mmps / (f32) _count;
  right = _sumWheelSpeedR_mmps / (f32) _count;
}

u8 MovementComponent::UnexpectedMovement::GetMaxCount() const
{
  return _heldInPalmModeEnabled ? kMaxUnexpectedMovementCountWhileHeldInPalm : kMaxUnexpectedMovementCount;
}

} // namespace Vector
} // namespace Anki

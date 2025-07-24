/**
 * File: movementComponent.h
 *
 * Author: Lee Crippen
 * Created: 10/21/2015
 *
 * Description: Robot component to handle logic and messages associated with the robot moving.
 *
 * Copyright: Anki, Inc. 2015
 *
 **/

#ifndef __Anki_Cozmo_Basestation_Components_MovementComponent_H__
#define __Anki_Cozmo_Basestation_Components_MovementComponent_H__

#include "coretech/common/shared/types.h"
#include "coretech/common/engine/objectIDs.h"
#include "coretech/common/engine/robotTimeStamp.h"
#include "coretech/vision/engine/trackedFace.h"
#include "engine/components/animationComponent.h"
#include "util/entityComponent/entity.h"
#include "util/helpers/noncopyable.h"
#include "util/signals/signalHolder.h"
#include "util/signals/simpleSignal_fwd.h"
#include <list>
#include <map>

namespace Anki {
namespace Vector {
  
// declarations
class Robot;
struct RobotState;
class IExternalInterface;
enum class AnimTrackFlag : uint8_t;
  
class MovementComponent : public IDependencyManagedComponent<RobotComponentID>, private Util::noncopyable,
                          private Util::SignalHolder
{
public:
  using MotorActionID = u8;

  MovementComponent();
  virtual ~MovementComponent() { }

  //////
  // IDependencyManagedComponent functions
  //////
  virtual void InitDependent(Vector::Robot* robot, const RobotCompMap& dependentComps) override;
  virtual void GetInitDependencies(RobotCompIDSet& dependencies) const override {
    dependencies.insert(RobotComponentID::CozmoContextWrapper);
  };
  virtual void AdditionalInitAccessibleComponents(std::set<RobotComponentID>& components) const override{
    components.insert(RobotComponentID::Animation);
  };
  virtual void GetUpdateDependencies(RobotCompIDSet& dependencies) const override{};
  //////
  // end IDependencyManagedComponent functions
  //////
  
  void NotifyOfRobotState(const RobotState& robotState);
  
  void OnRobotDelocalized();
  
  void UpdateOdometers(const Vector::RobotState& robotState);

  // Checks for unexpected movement specifically while turning such as
  // - Cozmo is turning one direction but you turn him the other way
  // - Cozmo is turning one direction and you turn him faster so he overshoots his turn angle
  // - Cozmo is stuck on an object and is unable to turn
  void CheckForUnexpectedMovement(const RobotState& robotState);
  
  bool IsUnexpectedMovementDetected() const { return _unexpectedMovement.IsDetected(); }
  
  UnexpectedMovementSide GetUnexpectedMovementSide() const { return IsUnexpectedMovementDetected() ?
                                                                    _unexpectedMovementSide        :
                                                                    UnexpectedMovementSide::UNKNOWN; }
  
  // True if any motor speed (head, left, or wheels) is non-zero in most recent RobotState message
  bool   IsMoving()        const { return _isMoving; }
  
  // True if head/lift/wheels are moving
  bool   IsHeadMoving()    const { return _isHeadMoving; }
  bool   IsLiftMoving()    const { return _isLiftMoving; }
  bool   AreWheelsMoving() const { return _areWheelsMoving; }
  
  // Same as above, but looks up moving state in history, by given time
  // - If we fail finding the state in history, all methods return TRUE (to be conservative)
  // - These are non-const because they can insert things into pose history due to interpolation
  bool   WasMoving(RobotTimeStamp_t atTime); // Reminder: head, lift, or wheels!
  bool   WasHeadMoving(RobotTimeStamp_t atTime);
  bool   WasLiftMoving(RobotTimeStamp_t atTime);
  bool   WereWheelsMoving(RobotTimeStamp_t atTime);
  
  // Retrieves the timestamp at which one or more motors was last tracked as moving. If no robot
  // state has ever been received indicating movement, this will return a timestamp of 0!
  RobotTimeStamp_t GetLastTimeWasMoving() const { return _lastTimeWasMoving; }
  RobotTimeStamp_t GetLastTimeHeadWasMoving() const { return _lastTimeHeadWasMoving; }
  RobotTimeStamp_t GetLastTimeLiftWasMoving() const { return _lastTimeLiftWasMoving; }
  RobotTimeStamp_t GetLastTimeWheelsWereMoving() const { return _lastTimeWheelsWereMoving; }
  
  // Convenience methods for checking head OR wheels, since either moves the camera
  bool   IsCameraMoving() const { return _isHeadMoving || _areWheelsMoving; }
  bool   WasCameraMoving(RobotTimeStamp_t atTime); // Slightly more efficient than calling WasHeadMoving _and_ WereWheelsMoving
  RobotTimeStamp_t GetLastTimeCameraWasMoving() const;

  uint8_t GetTracksLockedBy(const std::string& who) const;
  uint8_t GetLockedTracks() const;

  // Returns true if any of the tracks are locked
  bool AreAnyTracksLocked(u8 tracks) const;
  // Returns true if all of the specified tracks are locked
  bool AreAllTracksLocked(u8 tracks) const;
  // Returns true if all the specified tracks are locked by 'who'
  bool AreAllTracksLockedBy(u8 tracks, const std::string& who) const;
  
  // The string 'who' indicates who is locking the tracks
  // In order to unlock tracks, the unlock 'who' needs to match the 'who' that did the locking
  void LockTracks(u8 tracks, const std::string& who, const std::string& debugName);
  // Returns true if there are any locks on tracks after unlocking tracks locked by 'who'
  bool UnlockTracks(u8 tracks, const std::string& who);
  
  // Converts int who to a string (used to easily allow actions to lock tracks with their tag)
  void LockTracks(u8 tracks, const int who, const std::string& debugName) { LockTracks(tracks, std::to_string(who), debugName); }
  bool UnlockTracks(u8 tracks, const int who) { return UnlockTracks(tracks, std::to_string(who)); }

  // Notify the movement component that updates should be applied up to and including the specified relative stream time
  // If shouldClearUnusedLocks is true any locks not applied on this function call will be dropped
  void ApplyUpdatesForStreamTime(const TimeStamp_t relativeStreamTime_ms, const bool shouldClearUnusedLocks = true);

  // Occassionally we want to lock/unlock a track partway through an animation - calling this function
  // will cause the locking/unlocking operation to happen at the timestamp specified in the currently
  // streaming animation
  // NOTE: While the lock/unlock will be appropriately applied in the animation streamer, there may be delays between
  // when the face is locked as visible to the user and when the functions that return locked tracks above are updated
  // to reflect the true robot state
  void LockTracksAtStreamTime(const u8 tracks, const TimeStamp_t relativeStreamTime_ms, const bool applyBeforeTick, 
                              const std::string& who, const std::string& debugName);
  void UnlockTracksAtStreamTime(const u8 tracks, const TimeStamp_t relativeStreamTime_ms, const bool applyBeforeTick,
                                const std::string& who);
  
  void UnlockAllTracks();
  
  // Sends calibrate command to robot
  Result CalibrateMotors(bool head, bool lift, const MotorCalibrationReason& reason);
  
  // Enables lift power on the robot.
  // If disabled, lift goes limp.
  Result EnableLiftPower(bool enable);
  
  // Enables head power on the robot.
  // If disabled, head goes limp.
  Result EnableHeadPower(bool enable);
  
  // Below are low-level actions to tell the robot to do something "now"
  // without using the ActionList system:
  
  // Sends a message to the robot to move the lift to the specified angle.
  // When the command is received by the robot, it returns a MotorActionAck
  // message with the ID that can optionally be written to actionID_out.
  Result MoveLiftToAngle(const f32 angle_rad,
                          const f32 max_speed_rad_per_sec,
                          const f32 accel_rad_per_sec2,
                          const f32 duration_sec = 0.f,
                          MotorActionID* actionID_out = nullptr);
                          
  // Sends a message to the robot to move the lift to the specified height.
  // When the command is received by the robot, it returns a MotorActionAck
  // message with the ID that can optionally be written to actionID_out.
  Result MoveLiftToHeight(const f32 height_mm,
                          const f32 max_speed_rad_per_sec,
                          const f32 accel_rad_per_sec2,
                          const f32 duration_sec = 0.f,
                          MotorActionID* actionID_out = nullptr);
  
  // Sends a message to the robot to move the head to the specified angle.
  // When the command is received by the robot, it returns a MotorActionAck
  // message with the ID that can optionally be written to actionID_out.
  Result MoveHeadToAngle(const f32 angle_rad,
                         const f32 max_speed_rad_per_sec,
                         const f32 accel_rad_per_sec2,
                         const f32 duration_sec = 0.f,
                         MotorActionID* actionID_out = nullptr);

  // Sends raw wheel commands to robot
  // Generally, you should be using an action instead of calling this directly
  // unless you have a need to run the wheels for an indeterminate amount of time.
  Result DriveWheels(const f32 lWheel_speed_mmps,
                     const f32 rWheel_speed_mmps,
                     const f32 lWheel_accel_mmps2,
                     const f32 rWheel_accel_mmps2);
  
  // Sends a message to the robot to turn in place to the specified angle.
  // When the command is received by the robot, it returns a MotorActionAck
  // message with the ID that can optionally be written to actionID_out.
  Result TurnInPlace(const f32 angle_rad,              // Absolute angle the robot should achieve at the end of the turn.
                     const f32 max_speed_rad_per_sec,  // Maximum speed of the turn (sign determines the direction of the turn)
                     const f32 accel_rad_per_sec2,     // Accel/decel to use during the turn
                     const f32 angle_tolerance,
                     const u16 num_half_revolutions,   // Used for turns greater than 180 degrees (e.g. "turn 720 degrees")
                     bool use_shortest_direction,      // If true, robot should take the shortest path to angle_rad and disregard
                                                       // the sign of max_speed and num_half_revolutions. If false, consider sign
                                                       // of max_speed and num_half_revolutions to determine which direction to
                                                       // turn and how far to turn (e.g. for turns longer than 360 deg)
                     MotorActionID* actionID_out = nullptr);

  
  // Flag the eye shift layer with the given name for removal next time head moves.
  // You may optionally specify the duration of the layer removal (i.e. how
  // long it takes to return to not making any face adjustment)
  void RemoveEyeShiftWhenHeadMoves(const std::string& name, TimeStamp_t duration_ms=0);
  
  Result StopAllMotors();
  Result StopHead();
  Result StopLift();
  Result StopBody();
  
  // Tracking is handled by actions now, but we will continue to maintain the
  // state of what is being tracked in this class.
  const ObjectID& GetTrackToObject() const { return _trackToObjectID; }
  void  SetTrackToObject(ObjectID objectID) { _trackToObjectID = objectID; }
  void  UnSetTrackToObject() { _trackToObjectID.UnSet(); }
  
  template<typename T>
  void HandleMessage(const T& msg);
  
  void PrintLockState() const;
  
  // Returns a string of who is locking each of the specified tracks
  std::string WhoIsLocking(u8 tracks) const;
  
  bool IsDirectDriving() const { return ((_drivingWheels || _drivingHead || _drivingLift)); }
  
  u8 GetMaxUnexpectedMovementCount() const { return _unexpectedMovement.GetMaxCount(); }

  // Call this if you would like to allow 'external' movement commands (e.g. raw DriveWheels command). These should
  // really only be allowed for things like SDK and Webots. The requester name is used to track where the request came
  // from so that commands can be dis-allowed when no one needs them anymore.
  void AllowExternalMovementCommands(const bool enable, const std::string& requester);
  
  // Enable/disable detection of rotation without motors. This must be explicitly enabled since it
  // differs from the most common use case of this component.
  void EnableUnexpectedRotationWithoutMotors(bool enabled) { _enableRotatedWithoutMotors = enabled; }
  bool IsUnexpectedRotationWithoutMotorsEnabled() const { return _enableRotatedWithoutMotors; }
  
  // Enable/disable detection of unexpected movement even when picked up, and clamps the max angular
  // velocity of any point-turn to the value specified by kMaxHeldInPalmTurnSpeed_radps. Must be
  // explicitly enabled by the HeldInPalmTracker component since that component enables behaviors
  // with point-turn actions to run even when the robot is picked up.
  void EnableHeldInPalmMode(const bool enabled);
  bool IsHeldInPalmModeEnabled() const { return _heldInPalmModeEnabled; }
    
  // Returns body distance traveled
  // i.e. Average wheel speed integrated over time
  double GetBodyOdometer_mm() const { return _body_odom_mm; }

  // Returns individual wheel distance traveled
  double GetLeftWheelOdometer_mm()  const { return _lWheel_odom_mm; }
  double GetRightWheelOdometer_mm() const { return _rWheel_odom_mm; }


  // Whether or not the encoders have been "disabled". 
  // (In reality they are operating at a lower frequency so that motion can be detected.)
  // This happens normally if the motors are not actively being driven.
  bool AreEncodersDisabled() const { return _areEncodersDisabled; }

  // Whether or not the head was detected to have moved while the encoders were "disabled"
  // i.e. Calibration is necessary!
  bool IsHeadEncoderInvalid() const { return _isHeadEncoderInvalid; }

  // Whether or not the lift was detected to have moved while the encoders were "disabled"
  // i.e. Calibration is necessary!
  bool IsLiftEncoderInvalid() const { return _isLiftEncoderInvalid; }

private:
  
  void InitEventHandlers(IExternalInterface& interface);
  int GetFlagIndex(uint8_t flag) const;
  AnimTrackFlag GetFlagFromIndex(int index);
  
  // Checks if the speed is near zero and if it is sets flag to false and unlocks tracks
  // otherwise it will set flag to true and lock the tracks if they are not locked
  void DirectDriveCheckSpeedAndLockTracks(f32 speed, bool& flag, u8 tracks, const std::string& who,
                                          const std::string& debugName);

  // Return an actionID for the next motor command to be sent to robot.
  // Also writes same actionID to actionID_out if it's non-null.
  MotorActionID GetNextMotorActionID(MotorActionID* actionID_out = nullptr);

  // Processes all currently locked tracks in addition to all delayed locks/unlocks
  // in order to get the full enumeration of tracks that will be locked at a given point
  // in time
  u8 GetTracksLockedAtRelativeStreamTime(const TimeStamp_t relativeStreamTime_ms) const;
  
  void SetAllowExternalMovementCommands(const bool enable);
  
  Robot* _robot = nullptr;
  
  MotorActionID _lastMotorActionID = 0;
  
  RobotTimeStamp_t _lastTimeWasMoving = 0;
  RobotTimeStamp_t _lastTimeHeadWasMoving = 0;
  RobotTimeStamp_t _lastTimeLiftWasMoving = 0;
  RobotTimeStamp_t _lastTimeWheelsWereMoving = 0;
  
  bool _isMoving = false;
  bool _isHeadMoving = false;
  bool _isLiftMoving = false;
  bool _areWheelsMoving = false;
  bool _areEncodersDisabled = false;
  bool _isHeadEncoderInvalid = false;
  bool _isLiftEncoderInvalid = false;
  bool _enableRotatedWithoutMotors = false;
  bool _heldInPalmModeEnabled = false;

  // True if we are currently allowed to handle raw motion commands from the outside world
  // (e.g. while SDK or Webots is active)
  bool _allowExternalMovementCommands = false;
  
  // Keep track of who requested to allow external movement commands (e.g. "sdk" or "ui")
  std::set<std::string> _allowExternalMovementCommandNames;
 
  std::list<Signal::SmartHandle> _eventHandles;
  
  // Object being tracked
  ObjectID _trackToObjectID;
  
  //bool _trackWithHeadOnly = false;
  
  struct EyeShiftToRemove {
    TimeStamp_t duration_ms;
    bool        headWasMoving;
  };
  std::unordered_map<std::string, EyeShiftToRemove> _eyeShiftToRemove;
  
  // Helper class for detecting unexpected movement
  class UnexpectedMovement
  {
    RobotTimeStamp_t  _startTime;
    f32               _sumWheelSpeedL_mmps;
    f32               _sumWheelSpeedR_mmps;
    u8                _count;
    
    bool              _heldInPalmModeEnabled = false;
    
  public:
    
    UnexpectedMovement() { Reset(); }
    
    static const u8  kMaxUnexpectedMovementCount;
    
    u8               GetMaxCount()  const;
    u8               GetCount()     const { return _count; }
    RobotTimeStamp_t GetStartTime() const { return _startTime; }
    void             GetAvgWheelSpeeds(f32& left, f32& right) const;
    
    bool IsDetected() const;
    void Increment(u8 countInc, f32 leftSpeed_mmps, f32 rightSpeed_mmps, RobotTimeStamp_t currentTime);
    void Decrement();
    void Reset();
    void EnableHeldInPalmMode(const bool enable) { _heldInPalmModeEnabled = enable; }
  };
  
  UnexpectedMovement _unexpectedMovement;
  
  UnexpectedMovementSide _unexpectedMovementSide = UnexpectedMovementSide::UNKNOWN;
  
  const f32 kGyroTol_radps                 = DEG_TO_RAD(10);
  const f32 kWheelDifForTurning_mmps       = 30;
  const f32 kMinWheelSpeed_mmps            = 20;
  const f32 kMaxHeldInPalmTurnSpeed_radps  = DEG_TO_RAD(40);
  const f32 kExpectedVsActualGyroTol_radps = 0.2f;
  
  // Flags for whether or not we are currently directly driving the following motors

  bool _drivingWheels     = false;
  bool _drivingHead       = false;
  bool _drivingLift       = false;
  const char* kDrivingWheelsStr  = "DirectDriveWheels";
  const char* kDrivingHeadStr    = "DirectDriveHead";
  const char* kDrivingLiftStr    = "DirectDriveLift";
  const char* kDrivingArcStr     = "DirectDriveArc";
  const char* kDrivingTurnStr    = "DirectDriveTurnInPlace";
  const char* kOnChargerInSdkStr = "OnChargerInSDK";
  
  const u8 kAllMotorTracks = ((u8)AnimTrackFlag::HEAD_TRACK |
                              (u8)AnimTrackFlag::LIFT_TRACK |
                              (u8)AnimTrackFlag::BODY_TRACK);

  // Odometer
  double _body_odom_mm   = 0.0;
  double _lWheel_odom_mm = 0.0;
  double _rWheel_odom_mm = 0.0;


  struct LockInfo
  {
    std::string who;
    std::string debugName;
    
    inline const bool operator<(const LockInfo& other) const
    {
      return who < other.who;
    }
    
    inline const bool operator==(const LockInfo& other) const
    {
      return who == other.who;
    }
  };

  struct DelayedTrackLockInfo{
    DelayedTrackLockInfo(u8 tracksToLock, const std::string& who)
    : tracksToLock(tracksToLock)
    , who(who){}
    u8 tracksToLock;
    std::string who;
  };

  std::array<std::multiset<LockInfo>, (size_t)AnimConstants::NUM_TRACKS> _trackLockCount;

  // maps relative stream timestamps to information about how to lock tracks when that timestamp is reached
  std::multimap<uint32_t, DelayedTrackLockInfo> _delayedTracksToLock;
  std::multimap<uint32_t, DelayedTrackLockInfo> _delayedTracksToUnlock;
  AnimationComponent* _animComponent;

}; // class MovementComponent
  
  
} // namespace Vector
} // namespace Anki

#endif //  __Anki_Cozmo_Basestation_Components_MovementComponent_H__

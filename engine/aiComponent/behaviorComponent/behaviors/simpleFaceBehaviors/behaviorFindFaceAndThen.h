/**
 * File: behaviorFindFaceAndThen.h
 *
 * Author: ross
 * Created: 2018-06-22
 *
 * Description: Finds a face either in the activation direction, or wherever one was last seen,
 *              and if it finds one, either delegates to a followup behavior or exits
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#ifndef __Engine_AiComponent_BehaviorComponent_Behaviors_BehaviorFindFaceAndThen__
#define __Engine_AiComponent_BehaviorComponent_Behaviors_BehaviorFindFaceAndThen__

#include "engine/aiComponent/behaviorComponent/behaviors/iCozmoBehavior.h"
#include "clad/types/salientPointTypes.h"
#include "coretech/common/engine/robotTimeStamp.h"
#include "engine/smartFaceId.h"

namespace Anki {
namespace Vector {
  
enum class AnimationTrigger : int32_t;
class BehaviorSearchWithinBoundingBox;
class ISimpleFaceBehavior;

class BehaviorFindFaceAndThen : public ICozmoBehavior
{
public:
  // Returns the face found while running this behavior. It is reset upon activation, and persists after deactivation.
  SmartFaceID GetFoundFace() const { return _dVars.targetFace; }
  
protected:
  // Enforce creation through BehaviorFactory
  friend class BehaviorFactory;
  BehaviorFindFaceAndThen(const Json::Value& config);

  virtual void GetBehaviorOperationModifiers(BehaviorOperationModifiers& modifiers) const override;
  virtual void GetBehaviorJsonKeys(std::set<const char*>& expectedKeys) const override;
  void GetAllDelegates(std::set<IBehavior*>& delegates) const override;

  virtual void InitBehavior() override;
  virtual void OnBehaviorActivated() override;
  virtual void BehaviorUpdate() override;
  virtual void OnBehaviorDeactivated() override;
  
  virtual bool WantsToBeActivatedBehavior() const override;
  
private:
  
  // "Look" is looking in the current direction
  // "Search" is reorienting toward multiple directions
  
  enum class State {
    Invalid=0,
    DriveOffCharger,
    LiftingHead,
    LookForFaceInMicDirection,
    TurnTowardsPreviousFace,
    FindFaceInCurrentDirection,
    SearchForFace,
    FollowupBehavior,
  };
  
  struct InstanceConfig {
    InstanceConfig();
    
    bool alwaysDetectFaces;
    
    float timeUntilCancelFaceLooking_s;
    float timeUntilCancelSearching_s;
    float timeUntilCancelFollowup_s;
    
    // when looking in a specific direction for a face, _extend_ timeUntilCancelFaceLooking_s by
    // this much time if a body is seen, and run a find-face-from-body behavior for the remainder
    float additionalLookTimeIfSawBody_s;
    // when searching all over for a face, _extend_ timeUntilCancelFaceSearching_s by this much time,
    // and run the find-face-from-body behavior for only this long. Afterwards, if the total time
    // searching for a face is still less than the updated timeUntilCancelSearching_s, continue
    // the original search behavior
    float additionalSearchTimeIfSawBody_s;
    
    // if it starts on the charger, if can either leave the charger before looking for a face, or stay
    // on the charger looking for a face. If it leaves the charger, it will subsequently turn to the last seen face
    bool shouldLeaveChargerFirst;
    
    // if true, this behavior assumes it started facing in the dominant mic direction, unless it was on the charger.
    bool startedWithMicDirection;
    
    std::string searchBehaviorID;
    ICozmoBehaviorPtr searchFaceBehavior; // if set, this behavior will search for a face if one is not found
    
    std::string driveOffChargerBehaviorID;
    ICozmoBehaviorPtr driveOffChargerBehavior;
    
    std::string behaviorOnceFoundID;
    ICozmoBehaviorPtr behaviorOnceFound;
    bool behaviorOnceFoundIsSimpleFace;
    bool exitOnceFound;
    
    bool useBodyDetector;
    std::shared_ptr<BehaviorSearchWithinBoundingBox> behaviorFindFaceInBB;
    float upperPortionLookUpPercent;
    
    AnimationTrigger animWhenSeesFace;
    AnimationTrigger animWhileSearching;
  };

  struct DynamicVariables {
    DynamicVariables();
    State currentState;
    
    float stateEndTime_s;
    float stateMinTime_s;
    
    SmartFaceID targetFace;
    RobotTimeStamp_t lastFaceTimeStamp_ms;
    
    RobotTimeStamp_t activationTimeStamp_ms;
    
    RobotTimeStamp_t timeAtStateChange;
    bool sawBodyDuringState;
    Vision::SalientPoint lastPersonDetected;
    float timeEndFaceFromBodyBehavior_s;

    // track faces that failed so we don't keep trying to look at them
    std::vector<SmartFaceID> failedFaces;
    
    u32 animWhileSearchingTag;
    
    // for DAS
    bool sawBody;
    bool sawFace;
    bool sentEvent;
  };

  InstanceConfig   _iConfig;
  DynamicVariables _dVars;
  
  void TransitionToDrivingOffCharger();
  void TransitionToLookingInMicDirection();
  void TransitionToTurningTowardsFace();
  void TransitionToFindingFaceInCurrentDirection();
  void TransitionToSearchingForFace();
  void TransitionToFollowupBehavior();
  
  // If there is a face, and it is the most recent, and it shares the same origin, this returns true and sets the params
  bool GetRecentFaceSince( RobotTimeStamp_t sinceTime_ms, SmartFaceID& faceID, RobotTimeStamp_t& timeStamp_ms );
  bool GetRecentFace( SmartFaceID& faceID, RobotTimeStamp_t& timeStamp_ms );
  
  bool GetRecentBodySince( RobotTimeStamp_t sinceTime_ms, Vision::SalientPoint& person ) const;
  
  // Adds an action without changing the state
  void RunFindFaceFromBodyAction();
  
  // Adds an action without changing the state
  void RunSearchFaceBehavior();
  
  void StopSearchAnimation();
  
  void SetState_internal(State state, const std::string& stateName);
  
  void SendCompletionDASEvent();

};


} // namespace Vector
} // namespace Anki


#endif // __Engine_AiComponent_BehaviorComponent_Behaviors_BehaviorFindFaceAndThen__

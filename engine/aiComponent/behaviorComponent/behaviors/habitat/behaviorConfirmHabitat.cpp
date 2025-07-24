/**
 * File: behaviorConfirmHabitat.cpp
 *
 * Author: Arjun Menon
 * Date:   06 04 2018
 *
 * Description:
 * behavior that represents the robot's deliberate maneuvers to confirm whether
 *  it is inside the habitat after a pickup and putdown, or driving off charger
 *
 * the robot will engage in discrete actions to get to a configuration on top
 * of the white line so that its front cliff sensors are triggered. At this
 * position, the behavior will take no more actions since we are in the pose
 * to confirm the habitat in.
 *
 *
 * Copyright: Anki, Inc. 2018
 **/

#include "engine/aiComponent/behaviorComponent/behaviors/habitat/behaviorConfirmHabitat.h"

#include "clad/types/behaviorComponent/behaviorIDs.h"
#include "clad/externalInterface/messageEngineToGame.h"

#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/beiRobotInfo.h"
#include "engine/aiComponent/behaviorComponent/behaviorContainer.h"
#include "engine/aiComponent/behaviorComponent/activeBehaviorIterator.h"
#include "engine/aiComponent/behaviorComponent/userIntentComponent.h"
#include "engine/actions/basicActions.h"
#include "engine/actions/driveToActions.h"
#include "engine/actions/compoundActions.h"
#include "engine/actions/animActions.h"
#include "engine/blockWorld/blockWorld.h"
#include "engine/blockWorld/blockWorldFilter.h"
#include "engine/cozmoContext.h"
#include "engine/components/habitatDetectorComponent.h"
#include "engine/components/visionComponent.h"
#include "engine/components/visionScheduleMediator/visionScheduleMediator.h"
#include "engine/components/pathComponent.h"
#include "engine/navMap/mapComponent.h"
#include "engine/externalInterface/externalInterface.h"
#include "engine/robot.h"
#include "engine/viz/vizManager.h"

#include "util/console/consoleInterface.h"

#include "anki/cozmo/shared/cozmoConfig.h"
#include "anki/cozmo/shared/cozmoEngineConfig.h"
#include "coretech/common/engine/math/pose.h"
#include "coretech/vision/engine/observableObject.h"
#include "coretech/common/engine/utils/timer.h"

#include <memory>

namespace Anki {
namespace Vector {

namespace
{
  static const std::set<BehaviorID> kPassiveBehaviorsToInterrupt = {
    BEHAVIOR_ID(Exploring),
    BEHAVIOR_ID(ExploringVoiceCommand),
    BEHAVIOR_ID(Observing),
  };

  static const char* kOnTreadsTimeCondition = "onTreadsTimeCondition";
  static const char* kSearchForChargerBehaviorKey = "searchForChargerBehavior";

  // minimum time between playing react to white animations
  static const float kReactToWhiteAnimCooldown_sec = 10.0f;

  // bit flag representation for each cliff sensor
  static const uint8_t kFL = (1<<Util::EnumToUnderlying(CliffSensor::CLIFF_FL));
  static const uint8_t kFR = (1<<Util::EnumToUnderlying(CliffSensor::CLIFF_FR));
  static const uint8_t kBL = (1<<Util::EnumToUnderlying(CliffSensor::CLIFF_BL));
  static const uint8_t kBR = (1<<Util::EnumToUnderlying(CliffSensor::CLIFF_BR));
  
  // the minimum number of readings to accumulate to
  // use to check for very close obstacles
  const int kMinReadingsForObstacleCheck = 6;
  
  // distance below which we treat the prox sensor as seeing
  // a too-close obstacle
  const f32 kObstacleIsCloseThreshold_mm = 24.0f;
  
  // number of ticks to wait for valid prox readings when
  // averaging the values to determine if a nearby obstacle
  // is too close that we need to backup away from
  const u32 kMaxTicksToWaitForProx = 10;
  
  // helper console variable to bypass the normal confirmHabitat
  // triggers to force it to run faster (dev and testing purpose)
  CONSOLE_VAR(bool, kDevForceBeginConfirmHabitat, "Habitat", false);
}

BehaviorConfirmHabitat::InstanceConfig::InstanceConfig()
{
  onTreadsTimeCondition.reset();
}

BehaviorConfirmHabitat::BehaviorConfirmHabitat(const Json::Value& config)
: Anki::Vector::ICozmoBehavior(config)
{
  SubscribeToTags({{
    EngineToGameTag::RobotStopped,
    EngineToGameTag::CliffEvent,
    EngineToGameTag::RobotOffTreadsStateChanged,
    EngineToGameTag::UnexpectedMovement
  }});

  BlockWorldFilter* chargerFilter = new BlockWorldFilter();
  chargerFilter->AddAllowedType(ObjectType::Charger_Basic);
  _chargerFilter.reset(chargerFilter);

  DEV_ASSERT(config.isMember(kOnTreadsTimeCondition), "ConfirmHabitat.Ctor.OnTreadsTimeConditionUndefined");
  _iConfig.onTreadsTimeCondition = BEIConditionFactory::CreateBEICondition(config[kOnTreadsTimeCondition], GetDebugLabel());
  ANKI_VERIFY(_iConfig.onTreadsTimeCondition != nullptr,
              "ConfirmHabitat.Ctor.OnTreadsTimeConditionNotConstructed",
              "OnTreadsTimeCondition specified, but did not build properly");
  
  _iConfig.searchForChargerBehaviorIDStr = JsonTools::ParseString(config, kSearchForChargerBehaviorKey, GetDebugLabel());
}

BehaviorConfirmHabitat::~BehaviorConfirmHabitat()
{
}

void BehaviorConfirmHabitat::InitBehavior()
{
  _dVars = DynamicVariables();
  if(_iConfig.onTreadsTimeCondition != nullptr){
    _iConfig.onTreadsTimeCondition->Init(GetBEI());
  }
  
  auto behaviorID = BehaviorTypesWrapper::BehaviorIDFromString(_iConfig.searchForChargerBehaviorIDStr);
  _iConfig.searchForChargerBehavior = GetBEI().GetBehaviorContainer().FindBehaviorByID(behaviorID);
  DEV_ASSERT(_iConfig.searchForChargerBehavior != nullptr, "ConfimHabitat.InitBehavior.NullSearchForChargerBehavior");
}

void BehaviorConfirmHabitat::GetAllDelegates(std::set<IBehavior*>& delegates) const
{
  delegates.insert(_iConfig.searchForChargerBehavior.get());
}

bool BehaviorConfirmHabitat::WantsToBeActivatedBehavior() const
{
  if(kDevForceBeginConfirmHabitat) {
    return true;
  }
  
  // note
  //
  // conditions defined here:
  // + robot must not be on the charger PLATFORM
  // + robot does not have user intent pending
  // + robot does not have user intent active
  //
  // conditions defined in json:
  // + robot has been putdown at least 30 seconds ago

  const bool onChargerPlatform = GetBEI().GetRobotInfo().IsOnChargerPlatform();

  const auto& uic = GetBEI().GetAIComponent().GetComponent<BehaviorComponent>().GetComponent<UserIntentComponent>();
  auto activeUserIntentPtr = uic.GetActiveUserIntent();
  const bool intentsPendingOrActive = uic.IsAnyUserIntentPending() || (activeUserIntentPtr != nullptr);

  const auto& habitatDetector = GetBEI().GetHabitatDetectorComponent();
  HabitatBeliefState hbelief  = habitatDetector.GetHabitatBeliefState();

  bool onTreadsTimeValid = false;
  if(_iConfig.onTreadsTimeCondition != nullptr) {
    onTreadsTimeValid = _iConfig.onTreadsTimeCondition->AreConditionsMet(GetBEI());
  }

  if(!onChargerPlatform && !intentsPendingOrActive && hbelief == HabitatBeliefState::Unknown && onTreadsTimeValid) {
    // it is costly to iterate the behavior stack
    // therefore we check the interrupt condition last
    bool canInterrupt = false;
    // note: this function is called on every active behavior on the stack
    //  we look for any of the "passive" behaviors that we can interrupt
    //  otherwise we do not want to interrupt any other acting going on
    //
    // important: this function is the place to insert any special case logic
    //  where a behavior should not be interrupted
    const auto checkInterruptCallback = [&canInterrupt](const ICozmoBehavior& behavior)->bool {
      auto got = kPassiveBehaviorsToInterrupt.find(behavior.GetID());
      if(got != kPassiveBehaviorsToInterrupt.end()) {
        canInterrupt = true;
        return false; // canInterrupt will not change from here, stop iterating
      }
      return true; // keep iterating
    };
    const auto& behaviorIterator = GetBehaviorComp<ActiveBehaviorIterator>();
    behaviorIterator.IterateActiveCozmoBehaviorsForward( checkInterruptCallback, nullptr );
    return canInterrupt;
  }
  // if control reaches here, robot EITHER:
  // - is on the charger platform
  // - has a user intent pending/active
  // - knows its habitat belief state already
  // - has been on the treads for long enough
  return false;
}

void BehaviorConfirmHabitat::GetBehaviorJsonKeys(std::set<const char*>& expectedKeys) const
{
  expectedKeys.insert(kOnTreadsTimeCondition);
  expectedKeys.insert(kSearchForChargerBehaviorKey);
}

void BehaviorConfirmHabitat::OnBehaviorActivated()
{
  PRINT_CH_INFO("Behaviors", "ConfirmHabitat.Activated","");
  
  // disable stop-on-white until we finished looking for the charger
  GetBEI().GetCliffSensorComponent().EnableStopOnWhite(false);

  _dVars = DynamicVariables();
}

void BehaviorConfirmHabitat::OnBehaviorDeactivated()
{
  PRINT_CH_INFO("Behaviors", "ConfirmHabitat.Deactivated","");
  GetBEI().GetCliffSensorComponent().EnableStopOnWhite(false);
  _dVars = DynamicVariables();
}

void BehaviorConfirmHabitat::OnBehaviorEnteredActivatableScope()
{
  if(_iConfig.onTreadsTimeCondition != nullptr) {
    _iConfig.onTreadsTimeCondition->SetActive(GetBEI(), true);
  }
}

void BehaviorConfirmHabitat::OnBehaviorLeftActivatableScope()
{
  if(_iConfig.onTreadsTimeCondition != nullptr) {
    _iConfig.onTreadsTimeCondition->SetActive(GetBEI(), false);
  }
}
  
bool BehaviorConfirmHabitat::CheckIfFrontCliffsAreInLogoRegion()
{
  const auto getCliffSensorPoseWRTLogo = [this](float x, float y)->std::pair<bool,Pose3d>
  {
    Pose3d cliffPoseWrtLogo;
    cliffPoseWrtLogo.SetParent(GetBEI().GetRobotInfo().GetPose());
    cliffPoseWrtLogo.SetTranslation({x, y, 0.0f});
    
    const ObservableObject* charger = GetChargerIfObserved();
    if(charger==nullptr) {
      return {false, Pose3d()};
    }
    
    Pose3d logoCenterPose;
    logoCenterPose.SetParent(charger->GetPose());
    // based on CAD drawings, the distance between the object origin
    // of the charger, and the origin of the logo center is 54mm
    logoCenterPose.SetTranslation({-54.0f, 0.f, 0.f});
    
    // result is false if they are not in connected pose trees
    Pose3d out;
    bool result = cliffPoseWrtLogo.GetWithRespectTo(logoCenterPose, out);
    out.ClearParent();
    return {result, out};
  };
  
  auto gotFL = getCliffSensorPoseWRTLogo(kCliffSensorXOffsetFront_mm, kCliffSensorYOffset_mm);
  if(!gotFL.first) {
    return false;
  }
  
  auto gotFR = getCliffSensorPoseWRTLogo(kCliffSensorXOffsetFront_mm, -kCliffSensorYOffset_mm);
  if(!gotFR.first) {
    return false;
  }
  
  // Logo can be approximated as a 30mm x 70mm rectangle
  // we pad these measurements to 50mm x 90mm to account for slop in charger pose-estimate
  // these measurements are taken using CAD drawings
  // note: the LogoCenter is slightly offset inside the rectangle
  const AxisAlignedQuad aabb({15,45}, {-35,-45}); // in the frame of the logo
  const bool FL = aabb.Contains(Point2f{gotFL.second.GetTranslation().x(),
                                 gotFL.second.GetTranslation().y()});
  const bool FR = aabb.Contains(Point2f{gotFR.second.GetTranslation().x(),
                                 gotFR.second.GetTranslation().y()});
  return FL || FR;
}

void BehaviorConfirmHabitat::BehaviorUpdate()
{
  if(!IsActivated()) {
    return;
  }

  if(IsControlDelegated()) {
    return;
  }

  const auto& habitatDetector = GetBEI().GetHabitatDetectorComponent();
  std::unique_ptr<IActionRunner> actionToExecute;
  RobotCompletedActionCallback actionCallback = nullptr;

  // play the reaction to discovering we are in the habitat, if we are in it
  if(habitatDetector.GetHabitatBeliefState() == HabitatBeliefState::InHabitat) {
    TransitionToReactToHabitat();
    return;
  } else if(habitatDetector.GetHabitatBeliefState() == HabitatBeliefState::NotInHabitat) {
    CancelSelf();
    return;
  }

  switch(_dVars._phase) {
    case ConfirmHabitatPhase::InitialCheckObstacle:
    {
      // when we first start this behavior, we may be very close
      // to the walls or other obstacles in the habitat. So our
      // first actions are to:
      // * observe the prox sensor data
      // * if there is an obstacle too close, backup and turn away
      // * otherwise continue with the rest of the behavior
      if(UpdateProxSensorFilter()) {
        if(CheckIsCloseToObstacle()) {
          // from all starting positions, where the cliff sensor is past
          // the white line, moving backwards by 2cm will get the front
          // cliff sensors to be now behind the line (excepting the case
          // where it is moving parallel to the white line).
          // in the moving-parallel case, turning in a random direction
          // will introduce a new bearing which MAY perturb the robot
          // in a direction where it will encounter the white line head on
          TransitionToBackupTurnForward(-20, RandomSign()*DEG_TO_RAD(30.0f), 0);
        }
        _dVars._phase = ConfirmHabitatPhase::InitialCheckCharger;
      }
      break;
    }
    case ConfirmHabitatPhase::InitialCheckCharger:
    {
      if(GetChargerIfObserved() != nullptr) {
        GetBEI().GetCliffSensorComponent().EnableStopOnWhite(true);
        _dVars._phase = ConfirmHabitatPhase::SeekLineFromCharger;
        TransitionToSeekLineFromCharger();
      } else {
        _dVars._phase = ConfirmHabitatPhase::LocalizeCharger;
        TransitionToLocalizeCharger();
      }
      break;
    }
    case ConfirmHabitatPhase::LocalizeCharger:
    {
      // check if we've seen the charger yet, or exceeded search turn angle
      const bool chargerSeen = GetChargerIfObserved() != nullptr;
      if(chargerSeen) {
        PRINT_CH_INFO("Behaviors", "ConfirmHabitat.BehaviorUpdate.LocalizeCharger.FoundCharger", "");
        _dVars._phase = ConfirmHabitatPhase::SeekLineFromCharger;
        TransitionToSeekLineFromCharger();
      } else {
        TransitionToLocalizeCharger();
      }
      break;
    }
    case ConfirmHabitatPhase::SeekLineFromCharger:
    {
      // only try this state once, when invoked
      // if we aren't LineDocking, then RandomWalk
      // note: the action-completion callback will
      //        perform the transition check
      if(habitatDetector.GetHabitatLineRelPose() != HabitatLineRelPose::Invalid &&
         habitatDetector.GetHabitatLineRelPose() != HabitatLineRelPose::AllGrey) {
        _dVars._phase = ConfirmHabitatPhase::LineDocking;
      } else {
        PRINT_CH_INFO("Behaviors", "ConfirmHabitat.BehaviorUpdate.SeekLine.NotStoppedOnWhite","");
        _dVars._phase = ConfirmHabitatPhase::RandomWalk;
        TransitionToRandomWalk();
      }
      break;
    }
    case ConfirmHabitatPhase::RandomWalk:
    {
      if(_dVars._randomWalkTooCloseObstacle) {
        // respond to unexpected movement
        TransitionToBackupTurnForward(-20, RandomSign()*M_PI_4_F, 0);
        _dVars._randomWalkTooCloseObstacle = false;
      } else if(habitatDetector.GetHabitatLineRelPose() == HabitatLineRelPose::Invalid ||
                habitatDetector.GetHabitatLineRelPose() == HabitatLineRelPose::AllGrey) {
        TransitionToRandomWalk();
      } else {
        _dVars._phase = ConfirmHabitatPhase::LineDocking;
      }
      break;
    }
    case ConfirmHabitatPhase::LineDocking:
    {
      switch(habitatDetector.GetHabitatLineRelPose()) {
        case HabitatLineRelPose::AllGrey:
        {
          PRINT_CH_INFO("Behaviors", "ConfirmHabitat.BehaviorUpdate.LineDocking.LineLost","");
          _dVars._phase = ConfirmHabitatPhase::RandomWalk;
          TransitionToRandomWalk();
          break;
        }
        case HabitatLineRelPose::WhiteFL: // deliberate fallthrough
        case HabitatLineRelPose::WhiteFR:
        {
          TransitionToCliffAlignWhite();
          break;
        }

        case HabitatLineRelPose::WhiteBL: // deliberate fallthrough
        case HabitatLineRelPose::WhiteBR:
        {
          // do not align to white using the back cliff sensors
          // instead move away from the white line
          f32 angle = habitatDetector.GetHabitatLineRelPose() == HabitatLineRelPose::WhiteBL ?
                      (M_PI_4_F) : (-M_PI_4_F);
          TransitionToTurnBackupForward(angle, 0, 30);
          break;
        }

        case HabitatLineRelPose::WhiteFLFR:
        {
          // desired state: do nothing
          break;
        }
        case HabitatLineRelPose::WhiteFRBR:
        {
          TransitionToTurnBackupForward(-M_PI_2_F, -10, 20);
          break;
        }
        case HabitatLineRelPose::WhiteFLBL:
        {
          TransitionToTurnBackupForward(M_PI_2_F, -10, 20);
          break;
        }
        case HabitatLineRelPose::WhiteBLBR:
        {
          TransitionToTurnBackupForward(M_PI_F, -10, 20);
          break;
        }
        case HabitatLineRelPose::Invalid:
        {
          break;
        }
      } // end switch
      break;
    }
  }
}

void BehaviorConfirmHabitat::DelegateActionHelper(IActionRunner* action, RobotCompletedActionCallback&& callback)
{
  if(action != nullptr) {
    PathMotionProfile slowProfile;
    slowProfile.speed_mmps = 40;
    slowProfile.pointTurnSpeed_rad_per_sec = 1.0;
    GetBEI().GetRobotInfo().GetPathComponent().SetCustomMotionProfileForAction(slowProfile, action);

    // add an action callback to print the result if we didn't assign one
    if(callback == nullptr) {
      callback = [](const ExternalInterface::RobotCompletedAction& msg)->void {
        switch(msg.result) {
          case ActionResult::SUCCESS: { break; }
          default:
          {
            PRINT_CH_INFO("Behaviors", "ConfirmHabitat.DelegateActionHelper.UnhandledActionResult","%s",EnumToString(msg.result));
          }
        }
      };
    }
    DelegateNow(action, callback);
  } else {
    PRINT_NAMED_WARNING("ConfirmHabitat.BehaviorUpdate.NoActionChosen","");
  }
}

void BehaviorConfirmHabitat::TransitionToReactToHabitat()
{
  PRINT_CH_INFO("Behaviors", "ConfirmHabitat.TransitionToReactToHabitat","");

  CompoundActionSequential* action = new CompoundActionSequential();

  // if StopOnWhite is enabled while playing an animation, then it will
  // interrupt that animation. In this case, however we don't want that.
  action->AddAction(
    new WaitForLambdaAction([](Robot& robot)->bool {
      robot.GetComponentPtr<CliffSensorComponent>()->EnableStopOnWhite(false);
      return true;
    },0.1f));

  action->AddAction(new TriggerAnimationAction(AnimationTrigger::ReactToHabitat, 1, true));

  // sometimes after playing an animation, the lift is blocking the prox
  action->AddAction(new MoveLiftToHeightAction(MoveLiftToHeightAction::Preset::LOW_DOCK));

  RobotCompletedActionCallback callback = [this](const ExternalInterface::RobotCompletedAction& msg)->void {
    this->CancelSelf();
  };

  DelegateActionHelper(action, std::move(callback));
}

void BehaviorConfirmHabitat::TransitionToLocalizeCharger()
{
  PRINT_CH_INFO("Behaviors", "ConfirmHabitat.TransitionToLocalizeCharger","");
  
  BehaviorSimpleCallback callback = [this](void)->void {
    // enable stop-on-white after we attempted to look for the charger
    GetBEI().GetCliffSensorComponent().EnableStopOnWhite(true);
    
    if(GetChargerIfObserved() != nullptr) {
      PRINT_CH_INFO("Behaviors", "ConfirmHabitat.TransitionToLocalizeCharger.ChargerFound","");
      _dVars._phase = ConfirmHabitatPhase::SeekLineFromCharger;
      TransitionToSeekLineFromCharger();
    } else {
      PRINT_CH_INFO("Behaviors", "ConfirmHabitat.TransitionToLocalizeCharger.ChargerNotFound","");
      _dVars._phase = ConfirmHabitatPhase::RandomWalk;
    }
  };
  
  // tick the wants to be activated condition once before delegating
  if( ANKI_VERIFY( _iConfig.searchForChargerBehavior->WantsToBeActivated(),
                   "BehaviorConfirmHabitat.TransitionToLocalizeCharger.DWTBA",
                   "'%s' doesn't want to activate",
                   _iConfig.searchForChargerBehavior->GetDebugLabel().c_str() ) )
  {
    DelegateNow(_iConfig.searchForChargerBehavior.get(), std::move(callback));
  } else {
    CancelSelf();
  }
}

void BehaviorConfirmHabitat::TransitionToCliffAlignWhite()
{
  PRINT_CH_INFO("Behaviors", "ConfirmHabitat.TransitionToCliffAlignWhite","%s",_dVars._cliffAlignRetry ? "Retry" : "");
  
  IActionRunner* action = new CliffAlignToWhiteAction();
  DEV_ASSERT_MSG(action != nullptr, "ConfirmHabitat.TransitionToCliffAlignWhite.NullActionPtr", "");
  
  RobotCompletedActionCallback callback = [this](const ExternalInterface::RobotCompletedAction& msg)->void {
    switch(msg.result) {
      case ActionResult::SUCCESS: { break; }
      case ActionResult::CLIFF_ALIGN_FAILED_TIMEOUT:
      {
        if(!_dVars._cliffAlignRetry) {
          // indicates we failed our first attempt at cliff alignment
          // + perturb our current position
          // + resume doing a random walk
          // the expected result is that the robot will eventually
          // attempt to do cliff alignment a second time
          // if that 2nd try fails, then the whole behavior will exit
          _dVars._cliffAlignRetry = true;
          _dVars._phase = ConfirmHabitatPhase::RandomWalk;
          TransitionToBackupTurnForward(-30, RandomSign()*DEG_TO_RAD(30.0f), 40);
          break;
        }
        // deliberate fall-through: if we already retried CliffAlignment
        // then we don't retry and instead fail to confirm the habitat
      }
      case ActionResult::CLIFF_ALIGN_FAILED_OVER_TURNING: // deliberate fall through
      {
        // this is probably indicative that we are not in the habitat
        // => force set the habitat detection state as NotInHabitat
        std::stringstream ss;
        ss << "BehaviorConfirmHabitat.";
        ss << EnumToString(msg.result);
        GetBEI().GetHabitatDetectorComponent().ForceSetHabitatBeliefState(HabitatBeliefState::NotInHabitat, ss.str());
        break;
      }
      case ActionResult::CLIFF_ALIGN_FAILED_STOPPED:
      case ActionResult::CLIFF_ALIGN_FAILED_NO_WHITE: // deliberate fall through
      {
        PRINT_NAMED_WARNING("ConfirmHabitat.TransitionToCliffAlignWhite.Failed", "%s", EnumToString(msg.result));
        break;
      }
      case ActionResult::CLIFF_ALIGN_FAILED_NO_TURNING:
      {
        PRINT_CH_INFO("Behaviors", "ConfirmHabitat.TransitionToCliffAlignWhite.Failed","");

        const auto& habitatDetector = GetBEI().GetHabitatDetectorComponent();
        auto linePose = habitatDetector.GetHabitatLineRelPose();
        switch(linePose) {
          case HabitatLineRelPose::AllGrey:
          {
            TransitionToBackupTurnForward(-30, RandomSign()*DEG_TO_RAD(30.0f), 30);
            break;
          }
          case HabitatLineRelPose::WhiteFL:
          case HabitatLineRelPose::WhiteFR:
          case HabitatLineRelPose::WhiteBL:
          case HabitatLineRelPose::WhiteBR:
          {
            TransitionToBackupTurnForward(-20, 0.0f, 0); // no turn requested here
            break;
          }
          default:
          {
            PRINT_NAMED_WARNING("ConfirmHabitat.TransitionToCliffAlignWhite.UnhandledLinePose",
                                  "%s", EnumToString(linePose));
            break;
          }
        }
        break;
      }
      default:
      {
        PRINT_CH_INFO("Behaviors", "ConfirmHabitat.TransitionToCliffAlignWhite.UnhandledActionResult",
                         "%s",EnumToString(msg.result));
      }
    }
  };
  DelegateActionHelper(action, std::move(callback));
}

void BehaviorConfirmHabitat::TransitionToReactToWhite(HabitatLineRelPose lineRelPose)
{
  u8 bitFlags = 0;
  switch(lineRelPose) {
    case HabitatLineRelPose::WhiteFL: { bitFlags = kFL; break; }
    case HabitatLineRelPose::WhiteFR: { bitFlags = kFR; break; }
    case HabitatLineRelPose::WhiteBL: { bitFlags = kBL; break; }
    case HabitatLineRelPose::WhiteBR: { bitFlags = kBR; break; }

    case HabitatLineRelPose::WhiteFLFR: { bitFlags = kFL | kFR; break; }
    case HabitatLineRelPose::WhiteFLBL: { bitFlags = kFL | kBL; break; }
    case HabitatLineRelPose::WhiteFRBR: { bitFlags = kFR | kBR; break; }
    case HabitatLineRelPose::WhiteBLBR: { bitFlags = kBL | kBR; break; }

    default:
    {
      bitFlags = 0;
      break;
    }
  }

  if(bitFlags == 0) {
    PRINT_NAMED_WARNING("ConfirmHabitat.TransitionReactToWhite.NoWhiteDetected","");
  }

  TransitionToReactToWhite(bitFlags);
}

void BehaviorConfirmHabitat::TransitionToReactToWhite(uint8_t whiteDetectedFlags)
{
  PRINT_CH_INFO("Behaviors", "ConfirmHabitat.TransitionToWhiteReactionAnim", "%d", (int)whiteDetectedFlags);

  const float currTime_sec = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
  const bool doWhiteReactionAnim = currTime_sec > _dVars._nextWhiteReactionAnimTime_sec;

  CompoundActionSequential* action = new CompoundActionSequential();

  if(doWhiteReactionAnim) {
    AnimationTrigger animToPlay;
    // Play reaction animation based on triggered sensor(s)
    switch (whiteDetectedFlags) {
      case (kFL | kFR): { animToPlay = AnimationTrigger::ExploringHuhClose;       break; }
      case kFL:         { animToPlay = AnimationTrigger::ExploringHuhClose;       break; }
      case kFR:         { animToPlay = AnimationTrigger::ExploringHuhClose;       break; }
      case kBL:
      case kBR:
      case (kBL | kBR):
      {
        // since Vector doesn't have eyes at the back
        // of his head, reacting to back cliffs does not
        // read properly to the user. Do nothing here.
        PRINT_CH_INFO("Behaviors", "ConfirmHabitat.TransitionToReactToWhite.BackCliffDetectingWhiteNoReact","");
        delete action;
        return;
      }
      default:
      {
        // This is some scary configuration that we probably shouldn't move from.
        delete(action);
        PRINT_NAMED_WARNING("ConfirmHabitat.TransitionToReactToWhite.InvalidCliffSensorConfig","");
        return;
      }
    }
    action->AddAction(new TriggerLiftSafeAnimationAction(animToPlay, 1, true, (u8)AnimTrackFlag::BODY_TRACK));

    // sometimes after playing an animation, the lift is blocking the prox
    action->AddAction(new MoveLiftToHeightAction(MoveLiftToHeightAction::Preset::LOW_DOCK));

    _dVars._nextWhiteReactionAnimTime_sec = currTime_sec + kReactToWhiteAnimCooldown_sec;
  }

  // insert a short wait to observe the line
  // when we receive a StopOnWhite message, there is a 1-2 tick lag for engine to register
  // that it is seeing a white line beneath it. This is used as a buffer for engine to
  // catch up and update its internal tracking of white
  action->AddAction(new WaitForLambdaAction([](Robot& robot)->bool {
                      return robot.GetHabitatDetectorComponent().GetHabitatLineRelPose() != HabitatLineRelPose::Invalid &&
                             robot.GetHabitatDetectorComponent().GetHabitatLineRelPose() != HabitatLineRelPose::AllGrey;
                    },0.1f));

  DelegateActionHelper(action, nullptr);
}

void BehaviorConfirmHabitat::TransitionToBackupTurnForward(int backDist_mm, f32 angle, int forwardDist_mm)
{
  PRINT_CH_INFO("Behaviors", "ConfirmHabitat.TransitionToBackupTurnForward","%d %4.2f %d", backDist_mm, angle, forwardDist_mm);
  CompoundActionSequential* action = new CompoundActionSequential(std::list<IActionRunner*>{
    // note: temporarily disable stop on white
    // this allows the robot to get out of tight spots
    // by allowing it to drive a little over the white
    new WaitForLambdaAction([](Robot& robot)->bool {
      robot.GetComponentPtr<CliffSensorComponent>()->EnableStopOnWhite(false);
      return true;
    },0.1f)
  });
  
  if(backDist_mm != 0) {
    action->AddAction(new DriveStraightAction(backDist_mm, 100));
  }
  
  if(!FLT_NEAR(angle, 0.0f)) {
    action->AddAction(new TurnInPlaceAction(angle, false));
  }
  
  action->AddAction(new WaitForLambdaAction([](Robot& robot)->bool {
    robot.GetComponentPtr<CliffSensorComponent>()->EnableStopOnWhite(true);
    return true;
  }));
  
  if(forwardDist_mm != 0) {
    action->AddAction(new DriveStraightAction(forwardDist_mm,  50));
  }
  
  DelegateActionHelper(action, nullptr);
}

void BehaviorConfirmHabitat::TransitionToTurnBackupForward(f32 angle, int backDist_mm, int forwardDist_mm)
{
  PRINT_CH_INFO("Behaviors", "ConfirmHabitat.TransitionToTurnBackupForward","");
  CompoundActionSequential* action = new CompoundActionSequential(std::list<IActionRunner*>{
    // note: temporarily disable stop on white
    // this allows the robot to get out of tight spots
    // by allowing it to drive a little over the white
    new WaitForLambdaAction([](Robot& robot)->bool {
      robot.GetComponentPtr<CliffSensorComponent>()->EnableStopOnWhite(false);
      return true;
    })
  });

  if(!FLT_NEAR(angle, 0.0f)) {
    action->AddAction(new TurnInPlaceAction(angle, false));
  }

  if(backDist_mm != 0) {
    action->AddAction(new DriveStraightAction(backDist_mm, 100));
  }

  action->AddAction(new WaitForLambdaAction([](Robot& robot)->bool {
      robot.GetComponentPtr<CliffSensorComponent>()->EnableStopOnWhite(true);
      return true;
    }));

  if(forwardDist_mm != 0) {
    action->AddAction(new DriveStraightAction(forwardDist_mm,  50));
  }
  DelegateActionHelper(action, nullptr);
}

void BehaviorConfirmHabitat::TransitionToRandomWalk()
{
  int index = GetRNG().RandInt(3);

  const Pose3d& robotPose = GetBEI().GetRobotInfo().GetPose();
  Pose3d desiredOffsetPose;
  if(index == 0) {
    desiredOffsetPose = Pose3d(Rotation3d(0, Z_AXIS_3D()), Vec3f(100,0,0));
  } else if(index == 1) {
    desiredOffsetPose = Pose3d(Rotation3d(-M_PI_4, Z_AXIS_3D()), Vec3f(75,-50,0));
  } else if(index == 2) {
    desiredOffsetPose = Pose3d(Rotation3d(M_PI_4, Z_AXIS_3D()), Vec3f(75,50,0));
  }
  desiredOffsetPose.SetParent(robotPose);
  GetBEI().GetMapComponent().SetUseProxObstaclesInPlanning(false);
  IActionRunner* action = new DriveToPoseAction(desiredOffsetPose);
  RobotCompletedActionCallback callback = [this](const ExternalInterface::RobotCompletedAction& msg)->void{
    GetBEI().GetMapComponent().SetUseProxObstaclesInPlanning(true);
  };
  DelegateActionHelper(action, std::move(callback));
  PRINT_CH_INFO("Behaviors", "ConfirmHabitat.TransitionToRandomWalk", "ActionNum=%d",index);
}

void BehaviorConfirmHabitat::TransitionToSeekLineFromCharger()
{
  // choose a candidate pose to drive towards the charger
  // + if we get RobotStopped with the reason=StopOnWhite => LineDocking
  const ObservableObject* charger = GetChargerIfObserved();
  if(!ANKI_VERIFY(charger != nullptr,"ConfirmHabitat.TransitionToSeekLineFromCharger.ChargerIsNull","")) {
    _dVars._phase = ConfirmHabitatPhase::RandomWalk;
    return;
  }

  // the origin of the charger located at the tip of the ramp
  // with the x-axis oriented towards the label
  const Pose3d& chargerPose = charger->GetPose();
  PRINT_CH_INFO("Behaviors", "ConfirmHabitat.SeekLineFromCharger","");

  // these poses are computed relative to the charger
  // the values selected define a position slightly beyond
  // the white line of the habitat, assuming the charger
  // is correctly installed in place
  static const std::vector<Pose3d> offsetList = {
    Pose3d(   M_PI_4_F, Z_AXIS_3D(), Vec3f(  53,  159, 0)),
    Pose3d(   M_PI_4_F, Z_AXIS_3D(), Vec3f(   0,  212, 0)),

    Pose3d(  -M_PI_4_F, Z_AXIS_3D(), Vec3f(  53, -159, 0)),
    Pose3d(  -M_PI_4_F, Z_AXIS_3D(), Vec3f(   0, -212, 0)),

    Pose3d( 3*M_PI_4_F, Z_AXIS_3D(), Vec3f(-141,  212, 0)),
    Pose3d( 3*M_PI_4_F, Z_AXIS_3D(), Vec3f(-194,  159, 0)),
    Pose3d( 3*M_PI_4_F, Z_AXIS_3D(), Vec3f(-247,  106, 0)),

    Pose3d(-3*M_PI_4_F, Z_AXIS_3D(), Vec3f(-141, -212, 0)),
    Pose3d(-3*M_PI_4_F, Z_AXIS_3D(), Vec3f(-194, -159, 0)),
    Pose3d(-3*M_PI_4_F, Z_AXIS_3D(), Vec3f(-247, -106, 0)),

    Pose3d(     M_PI_F, Z_AXIS_3D(), Vec3f(-325,    0, 0)),
  };

  std::vector<Pose3d> poses;
  poses.reserve(offsetList.size());
  for(int i=0; i<offsetList.size(); ++i) {
    Pose3d desiredOffsetPose = offsetList[i];
    desiredOffsetPose.SetParent(chargerPose);
    poses.push_back(desiredOffsetPose);
  }
  IActionRunner* action = new DriveToPoseAction(poses);
  DelegateActionHelper(action, nullptr);

}

const ObservableObject* BehaviorConfirmHabitat::GetChargerIfObserved() const
{
  const auto& robotPose = GetBEI().GetRobotInfo().GetPose();
  return GetBEI().GetBlockWorld().FindLocatedObjectClosestTo(robotPose, *_chargerFilter);
}

void BehaviorConfirmHabitat::HandleWhileActivated(const EngineToGameEvent& event)
{
  using namespace ExternalInterface;
  switch (event.GetData().GetTag())
  {
    case EngineToGameTag::CliffEvent:
    {
      // deliberate fallthrough
    }
    case EngineToGameTag::RobotOffTreadsStateChanged:
    {
      CancelDelegates();
      CancelSelf();
      break;
    }
    case EngineToGameTag::UnexpectedMovement:
    {
      PRINT_CH_INFO("Behaviors", "ConfirmHabitat.HandleWhiteActivated.UnexpectedMovement","");
      if(_dVars._phase == ConfirmHabitatPhase::RandomWalk && IsControlDelegated() ) {
        CancelDelegates();
        // we ran into an obstacle:
        // - confirm with the prox sensor
        // - backup, turn in a direction
        // - if there is no prox obstacle within the minimum gap => continue
        // - else turn some more, and stop
        const auto& proxData = GetBEI().GetRobotInfo().GetProxSensorComponent().GetLatestProxData();
        if(proxData.foundObject && proxData.distance_mm <= HabitatDetectorComponent::kConfirmationConfigProxMaxReading) {
          _dVars._randomWalkTooCloseObstacle = true;
        }
      }
      break;
    }
    default:
    {
      // nothing
      break;
    }
  } // end switch
}

void BehaviorConfirmHabitat::AlwaysHandleInScope(const EngineToGameEvent& event)
{
  using namespace ExternalInterface;
  switch(event.GetData().GetTag()) {
    case EngineToGameTag::RobotStopped:
    {
      const auto reason = event.GetData().Get_RobotStopped().reason;
      const auto belief = GetBEI().GetHabitatDetectorComponent().GetHabitatBeliefState();
      if(reason == StopReason::WHITE && belief == HabitatBeliefState::Unknown) {
        if(IsActivated()) {
          if(!CheckIfFrontCliffsAreInLogoRegion()) {
            PRINT_CH_INFO("Behaviors", "ConfirmHabitat.AlwaysHandleInScope.StoppedOnWhite","");
            TransitionToReactToWhite(event.GetData().Get_RobotStopped().whiteDetectedFlags);
          } else {
            // we are confident we are stopped on top of the logo
            int sign = 1;
            switch(event.GetData().Get_RobotStopped().whiteDetectedFlags) {
              case (kFL | kFR): { sign = RandomSign();  break; }
              case kFL:         { sign = -1;            break; }
              case kFR:         { sign = 1;             break; }
              default: {
                // other cliff-sensor configs for white stop are not valid
              }
            }
            // rotates away from the logo
            PRINT_CH_INFO("Behaviors", "ConfirmHabitat.AlwaysHandleInScope.StoppedOnWhite.InLogoRegion","");
            TransitionToTurnBackupForward(sign*M_PI_2, 0, 0);
          }
        }
      }
      break;
    }
    default:
    {
      break;
    }
  }
}
  
bool BehaviorConfirmHabitat::UpdateProxSensorFilter()
{
  const auto& proxData = GetBEI().GetComponentWrapper( BEIComponentID::ProxSensor ).GetComponent<ProxSensorComponent>().GetLatestProxData();
  // ignore the normal range spec since we are trying to perceive
  // very close obstacles and react to them
  if(proxData.foundObject) {
    _dVars._validProxDistances.push_back(proxData.distance_mm);
    // don't reset numTicksWaitingForProx here, we want the timeout to be hard
  }
  _dVars._numTicksWaitingForProx++;
  
  // whether enough readings are collected
  return (_dVars._numTicksWaitingForProx > kMaxTicksToWaitForProx)  ||
         (_dVars._validProxDistances.size() > kMinReadingsForObstacleCheck);
}
  
bool BehaviorConfirmHabitat::CheckIsCloseToObstacle() const
{
  u32 sum = 0;
  for(auto& reading : _dVars._validProxDistances) {
    sum += reading;
  }
  float averageDistance_mm = float(sum)/_dVars._validProxDistances.size();
  return averageDistance_mm < kObstacleIsCloseThreshold_mm;
}

}
}

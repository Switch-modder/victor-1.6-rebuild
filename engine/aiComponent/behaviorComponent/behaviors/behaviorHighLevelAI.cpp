/**
 * File: behaviorHighLevelAI.cpp
 *
 * Author: Brad Neuman
 * Created: 2017-10-16
 *
 * Description: Root behavior to handle the state machine for the high level AI of victor (similar to Cozmo's
 *              freeplay activities)
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#include "engine/aiComponent/behaviorComponent/behaviors/behaviorHighLevelAI.h"

#include "clad/types/behaviorComponent/behaviorTimerTypes.h"
#include "coretech/common/engine/jsonTools.h"
#include "coretech/common/engine/utils/timer.h"
#include "engine/aiComponent/behaviorComponent/behaviorContainer.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/beiRobotInfo.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/delegationComponent.h"
#include "engine/aiComponent/behaviorComponent/behaviorTimers.h"
#include "engine/aiComponent/behaviorComponent/userIntentComponent.h"
#include "engine/aiComponent/beiConditions/conditions/conditionAnyStimuli.h"
#include "engine/aiComponent/beiConditions/conditions/conditionLambda.h"
#include "engine/blockWorld/blockWorld.h"
#include "engine/blockWorld/blockWorldFilter.h"
#include "engine/faceWorld.h"
#include "util/console/consoleInterface.h"
#include "util/helpers/boundedWhile.h"
#include "util/logging/DAS.h"

namespace Anki {
namespace Vector {

#define LOG_CHANNEL "Behaviors"

#define CONSOLE_GROUP "BehaviorHighLevelAI"

// speed up high level AI with this. Be careful -- some conditions that check for time
// within a short interval [a,b] may not be met if you choose too fast a speedup factor
CONSOLE_VAR_RANGED(float, kTimeMultiplier, CONSOLE_GROUP, 1.0f, 1.0f, 300.0f);

// This is the _minumum_ cooldown to go from Observing to Exploring
CONSOLE_VAR(float, kHLAI_MinObservingBeforeExploring_s, CONSOLE_GROUP, 10.0f);
  
namespace {

constexpr const char* kDebugName = "BehaviorHighLevelAI";
  
const char* kSocializeKnownFaceCooldownKey = "socializeKnownFaceCooldown_s";
const char* kPlayWithCubeCooldownKey = "playWithCubeCooldown_s";
const char* kPlayWithCubeOnChargerCooldownKey = "playWithCubeOnChargerCooldown_s";
const char* kMaxFaceDistanceToSocializeKey = "maxFaceDistanceToSocialize_mm";
const char* kPostBehaviorSuggestionResumeOverrides = "postBehaviorSuggestionResumeOverrides"; // whew
  
const int kMaxTicksForPostBehaviorSuggestions = 5;
}

////////////////////////////////////////////////////////////////////////////////

BehaviorHighLevelAI::BehaviorHighLevelAI(const Json::Value& config)
  : InternalStatesBehavior( config, CreateCustomConditions() )
{
  _params.socializeKnownFaceCooldown_s = JsonTools::ParseFloat(config, kSocializeKnownFaceCooldownKey, kDebugName);
  _params.playWithCubeCooldown_s = JsonTools::ParseFloat(config, kPlayWithCubeCooldownKey, kDebugName);
  _params.playWithCubeOnChargerCooldown_s = JsonTools::ParseFloat(config, kPlayWithCubeOnChargerCooldownKey, kDebugName);
  _params.maxFaceDistanceToSocialize_mm = JsonTools::ParseFloat(config, kMaxFaceDistanceToSocializeKey, kDebugName);
  
  if( config[kPostBehaviorSuggestionResumeOverrides].isObject() ) {
    const auto& resumeOverrides = config[kPostBehaviorSuggestionResumeOverrides];
    for( const auto& suggestionName : resumeOverrides.getMemberNames() ) {
      PostBehaviorSuggestions suggestion = PostBehaviorSuggestions::Invalid;
      if( ANKI_VERIFY( PostBehaviorSuggestionsFromString( suggestionName, suggestion ),
                       "BehaviorHighLevelAI.Ctor.PBSInvalid",
                       "Post behavior suggestion '%s' not valid",
                       suggestionName.c_str() )
         && ANKI_VERIFY( suggestion != PostBehaviorSuggestions::Invalid,
                         "BehaviorHighLevelAI.Ctor.PBSActuallyInvalid",
                         "The 'Invalid' clad value was specified" ) )
      {
        std::string stateName = JsonTools::ParseString( resumeOverrides, suggestionName.c_str(), GetDebugLabel() );
        StateID state = GetStateID( stateName );
        if( ANKI_VERIFY( state != InvalidStateID,
                         "BehaviorHighLevelAI.Ctor.PBSInvalidState",
                         "State '%s' is no longer a state",
                         stateName.c_str() ) )
        {
          _params.pbsResumeOverrides.emplace( suggestion, state );
        }
      }
      
    }
  }
  
  MakeMemberTunable( _params.socializeKnownFaceCooldown_s, kSocializeKnownFaceCooldownKey, kDebugName );
  MakeMemberTunable( _params.playWithCubeOnChargerCooldown_s, kPlayWithCubeOnChargerCooldownKey, kDebugName );
  MakeMemberTunable( _params.playWithCubeCooldown_s, kPlayWithCubeCooldownKey, kDebugName );
  
  AddConsoleVarTransitions( "MoveToState", kDebugName );
}
  
void BehaviorHighLevelAI::GetBehaviorJsonKeys(std::set<const char*>& expectedKeys) const
{
  InternalStatesBehavior::GetBehaviorJsonKeys( expectedKeys );
  const char* list[] = {
    kSocializeKnownFaceCooldownKey,
    kPlayWithCubeCooldownKey,
    kPlayWithCubeOnChargerCooldownKey,
    kMaxFaceDistanceToSocializeKey,
    kPostBehaviorSuggestionResumeOverrides,
  };
  expectedKeys.insert( std::begin(list), std::end(list) );
}
  
BehaviorHighLevelAI::~BehaviorHighLevelAI()
{
  
}
  
void BehaviorHighLevelAI::BehaviorUpdate()
{
  InternalStatesBehavior::BehaviorUpdate();
}

CustomBEIConditionHandleList BehaviorHighLevelAI::CreateCustomConditions()
{
  CustomBEIConditionHandleList handles;
  
  const std::string emptyOwnerLabel = ""; // when these conditions are claimed from the factory, a label is added

  handles.emplace_back(
    BEIConditionFactory::InjectCustomBEICondition(
      "CloseFaceForSocializing",
      std::make_shared<ConditionLambda>(
        [this](BehaviorExternalInterface& bei) {
          const bool valueIfNeverRun = true;
          const auto& timer = GetBEI().GetBehaviorTimerManager().GetTimer( BehaviorTimerTypes::Socializing );
          const bool cooldownExpired = timer.HasCooldownExpired( _params.socializeKnownFaceCooldown_s / kTimeMultiplier, valueIfNeverRun );
          if( !cooldownExpired ) {
            // still on cooldown
            return false;
          }
          
          auto& faceWorld = bei.GetFaceWorld();
          const auto& faces = faceWorld.GetFaceIDs(0, true);
          for( const auto& faceID : faces ) {
            const auto* face = faceWorld.GetFace(faceID);
            if( face != nullptr ) {
              const Pose3d facePose = face->GetHeadPose();
              float distanceToFace = 0.0f;
              if( ComputeDistanceSQBetween( bei.GetRobotInfo().GetPose(),
                                            facePose,
                                            distanceToFace ) &&
                  distanceToFace < Util::Square(_params.maxFaceDistanceToSocialize_mm) ) {
                return true;
              }
            }
          }
          return false;
        },
        ConditionLambda::VisionModeSet{
          { VisionMode::Faces, EVisionUpdateFrequency::Low }
        },
        emptyOwnerLabel )));

  handles.emplace_back(
    BEIConditionFactory::InjectCustomBEICondition(
      "ChargerLocated",
      std::make_shared<ConditionLambda>(
        [](BehaviorExternalInterface& bei) {
          BlockWorldFilter filter;
          filter.SetFilterFcn( [](const ObservableObject* obj){
              return IsChargerType(obj->GetType(), false) && obj->IsPoseStateKnown();
            });
          const auto& blockWorld = bei.GetBlockWorld();
          const auto* block = blockWorld.FindLocatedMatchingObject(filter);
          return block != nullptr;
        },
        ConditionLambda::VisionModeSet{
          { VisionMode::Markers, EVisionUpdateFrequency::Low }
        },
        emptyOwnerLabel )));
  
  handles.emplace_back(
    BEIConditionFactory::InjectCustomBEICondition(
      "WantsToLeaveChargerForPlay",
      std::make_shared<ConditionLambda>(
        [this](BehaviorExternalInterface& bei) {
          // this keeps two separate timers, one for driving off the charger into play, and one for playing.
          // the latter is useful when seeing cubes when in the observing state, and is used by the
          // CloseCubeForPlaying strategy. The former decides whether the robot should leave the
          // charger to play. In case the robot finds no cube after leaving the charger, placing it
          // back on the charger won't cause it to immediately drive off in search of play.
          // Similarly, if a long time has elapsed since the robot last successfully drove off the charger
          // for playing, but it recently played with a cube, then it shouldn't try to drive off the charger
          // again.
          const bool valueIfNeverRun = false;
          const bool hasntDrivenOffChargerForPlay = StateExitCooldownExpired(
            GetStateID("ObservingDriveOffCharger"),
            _params.playWithCubeOnChargerCooldown_s / kTimeMultiplier,
            InternalStatesBehavior::StateCooldownDefault::False);

          const auto& timer = GetBEI().GetBehaviorTimerManager().GetTimer( BehaviorTimerTypes::PlayingWithCube );
          const bool hasntPlayed = timer.HasCooldownExpired(_params.playWithCubeCooldown_s / kTimeMultiplier, valueIfNeverRun);
          
          return hasntDrivenOffChargerForPlay && hasntPlayed;
        },
        /* No Vision Requirements*/
        emptyOwnerLabel )));

  handles.emplace_back(
    BEIConditionFactory::InjectCustomBEICondition(
      "CloseCubeForPlaying",
      std::make_shared<ConditionLambda>(
        [this](BehaviorExternalInterface& bei) {

          const auto& timer = GetBEI().GetBehaviorTimerManager().GetTimer( BehaviorTimerTypes::PlayingWithCube );
          if( !timer.HasCooldownExpired(_params.playWithCubeCooldown_s / kTimeMultiplier) ) {
            // still on cooldown
            return false;
          }

          BlockWorldFilter filter;
          filter.SetFilterFcn( [](const ObservableObject* obj) {
              return IsValidLightCube(obj->GetType(), false) && obj->IsPoseStateKnown();
            });
          const auto& blockWorld = bei.GetBlockWorld();
          const auto* block = blockWorld.FindLocatedMatchingObject(filter);
          return block != nullptr;
        },
        ConditionLambda::VisionModeSet{
          { VisionMode::Markers, EVisionUpdateFrequency::Low }
        },
        emptyOwnerLabel )));

  handles.emplace_back(
    BEIConditionFactory::InjectCustomBEICondition(
      "ExploringCooldownMet",
      std::make_shared<ConditionLambda>(
        [this](BehaviorExternalInterface& bei) {

          const float stateStartTime_s = GetLastTimeStarted( GetCurrentStateID() );
          const float currTime_s = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();

          if( currTime_s - stateStartTime_s < kHLAI_MinObservingBeforeExploring_s ) {
            // haven't been in observing for long enough
            return false;
          }

          const float cooldown_s = GetAIComp<AIWhiteboard>().GetExploringCooldown_s();

          if( _params.lastExploringCooldownPrinted != cooldown_s ) {
            LOG_INFO("BehaviorHighLevelAI.ExploringState.NewCooldown",
                     "Observing -> Exploring cooldown is now %f",
                     cooldown_s);
            _params.lastExploringCooldownPrinted = cooldown_s;
          }

          const bool cooldownPassed = StateExitCooldownExpired(
            GetStateID("Exploring"),
            cooldown_s,
            InternalStatesBehavior::StateCooldownDefault::UseFirstBehaviorActivationTime);
          return cooldownPassed;
        },
        emptyOwnerLabel )));

  return handles;
}
  
void BehaviorHighLevelAI::OverrideResumeState( StateID& resumeState )
{
  // get the most recent post-behavior suggestion that we care about
  size_t maxTick = 0;
  StateID maxTickState = InvalidStateID;
  for( const auto& configSuggestion : _params.pbsResumeOverrides ) {
    size_t tick = 0;
    if( GetAIComp<AIWhiteboard>().GetPostBehaviorSuggestion( configSuggestion.first, tick )
        && (tick >= maxTick) )
    {
      maxTick = tick;
      maxTickState = configSuggestion.second;
    }
  }
  // if there's a recent pending suggestion, consume it
  if( maxTickState != InvalidStateID ) {
    size_t currTick = BaseStationTimer::getInstance()->GetTickCount();
    if( maxTick + kMaxTicksForPostBehaviorSuggestions >= currTick ) {
      // override
      resumeState = maxTickState;
      GetAIComp<AIWhiteboard>().ClearPostBehaviorSuggestions();
    }
  }
}

void BehaviorHighLevelAI::OnStateNameChange( const std::string& oldStateName, const std::string& newStateName ) const
{
  // send a DAS msg. A lot of this is captured by the feature start/ending, but if a state holds more than one feature,
  // or if some states are transient, this helps to understand the whole behavior
  DASMSG(behavior_hlai_change, "behavior.hlai.change", "HLAI changed its state");
  DASMSG_SET(s1, newStateName, "The new state name");
  DASMSG_SET(s2, oldStateName, "The old state name");
  DASMSG_SEND();
}

}
}

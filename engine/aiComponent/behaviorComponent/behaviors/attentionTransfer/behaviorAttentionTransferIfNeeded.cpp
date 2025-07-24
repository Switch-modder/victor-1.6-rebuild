/**
 * File: BehaviorAttentionTransferIfNeeded.cpp
 *
 * Author: Brad
 * Created: 2018-06-19
 *
 * Description: This behavior will perform an attention transfer (aka "look at phone") animation and send the
 *              corresponding message to the app if needed. It can be configured to require an event happen X
 *              times in Y seconds to trigger the transfer, and if that hasn't been met it can either do a
 *              fallback animation or nothing.
 *
 * Copyright: Anki, Inc. 2018
 *
 **/


#include "engine/aiComponent/behaviorComponent/behaviors/attentionTransfer/behaviorAttentionTransferIfNeeded.h"

#include "clad/types/featureGateTypes.h"
#include "engine/actions/animActions.h"
#include "engine/actions/compoundActions.h"
#include "engine/aiComponent/behaviorComponent/attentionTransferComponent.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/beiRobotInfo.h"
#include "engine/cozmoContext.h"
#include "engine/utils/cozmoFeatureGate.h"
#include "util/cladHelpers/cladFromJSONHelpers.h"

namespace Anki {
namespace Vector {

namespace {
static const char* kReasonKey = "attentionTransferReason";
static const char* kAnimsIfNotRecentKey = "animsIfNotRecent";
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorAttentionTransferIfNeeded::BehaviorAttentionTransferIfNeeded(const Json::Value& config)
 : ICozmoBehavior(config)
{
  JsonTools::GetCladEnumFromJSON(config, kReasonKey, _iConfig.reason, GetDebugLabel());
  ANKI_VERIFY( RecentOccurrenceTracker::ParseConfig(config, _iConfig.numberOfTimes, _iConfig.amountOfSeconds),
               "BehaviorAttentionTransferIfNeeded.Constructor.InvalidConfig",
               "Behavior '%s' specified invalid recent occurrence config",
               GetDebugLabel().c_str() );

  // load anim triggers, if they exist
  for (const auto& triggerJson : config[kAnimsIfNotRecentKey])
  {
    AnimationTrigger anim;

    if( AnimationTriggerFromString( triggerJson.asString(), anim ) ) {
      _iConfig.animsIfNotRecent.push_back(anim);
    }
    else {
      PRINT_NAMED_ERROR("BehaviorAttentionTransferIfNeeded.AnimTriggers.Invalid",
                        "Behavior '%s' config specified an invalid animation trigger: '%s'",
                        GetDebugLabel().c_str(),
                        triggerJson.asCString());
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorAttentionTransferIfNeeded::~BehaviorAttentionTransferIfNeeded()
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorAttentionTransferIfNeeded::InitBehavior()
{
  if( ANKI_VERIFY( _iConfig.reason != AttentionTransferReason::Invalid,
                   "BehaviorAttentionTransferIfNeeded.Init.NoValidReason",
                   "Behavior '%s' does not have a valid attention transfer reason",
                   GetDebugLabel().c_str()) ) {
    _iConfig.recentOccurrenceHandle = GetBehaviorComp<AttentionTransferComponent>().GetRecentOccurrenceHandle(
      _iConfig.reason,
      _iConfig.numberOfTimes,
      _iConfig.amountOfSeconds);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorAttentionTransferIfNeeded::GetBehaviorJsonKeys(std::set<const char*>& expectedKeys) const
{
  const char* list[] = {
    kReasonKey,
    kAnimsIfNotRecentKey
  };
  expectedKeys.insert( std::begin(list), std::end(list) );
  RecentOccurrenceTracker::GetConfigJsonKeys(expectedKeys);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorAttentionTransferIfNeeded::WantsToBeActivatedBehavior() const
{
  const auto* featureGate = GetBEI().GetRobotInfo().GetContext()->GetFeatureGate();
  const bool featureEnabled = featureGate->IsFeatureEnabled(Anki::Vector::FeatureType::AttentionTransfer);
  return featureEnabled || !_iConfig.animsIfNotRecent.empty();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorAttentionTransferIfNeeded::OnBehaviorActivated()
{
  GetBehaviorComp<AttentionTransferComponent>().PossibleAttentionTransferNeeded( _iConfig.reason );

  const auto* featureGate = GetBEI().GetRobotInfo().GetContext()->GetFeatureGate();
  const bool featureEnabled = featureGate->IsFeatureEnabled(Anki::Vector::FeatureType::AttentionTransfer);

  if( featureEnabled && _iConfig.recentOccurrenceHandle->AreConditionsMet() ) {
    TransitionToAttentionTransfer();
  }
  else {
    TransitionToNoAttentionTransfer();
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorAttentionTransferIfNeeded::TransitionToAttentionTransfer()
{
  DelegateIfInControl(new TriggerLiftSafeAnimationAction(AnimationTrigger::LookAtDeviceGetIn), [this]() {
      GetBehaviorComp<AttentionTransferComponent>().OnAttentionTransferred( _iConfig.reason );

      CompoundActionSequential* action = new CompoundActionSequential({
            new TriggerLiftSafeAnimationAction(AnimationTrigger::LookAtDevice),
            new TriggerLiftSafeAnimationAction(AnimationTrigger::LookAtDeviceGetOut)});
      DelegateIfInControl(action);
    });
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorAttentionTransferIfNeeded::TransitionToNoAttentionTransfer()
{
  if( !_iConfig.animsIfNotRecent.empty() ) {
    CompoundActionSequential* action = new CompoundActionSequential();
    for( const auto& trigger : _iConfig.animsIfNotRecent ) {
      const bool ignoreFailure = true;
      action->AddAction(new TriggerLiftSafeAnimationAction(trigger), ignoreFailure);
    }

    DelegateIfInControl(action);
  }
}

}
}

/**
* File: behaviorStack.cpp
*
* Author: Kevin Karol
* Date:   8/17/2017
*
* Description: Class for representing and managing stack of behaviors
*
* Copyright: Anki, Inc. 2017
**/

#include "engine/aiComponent/behaviorComponent/behaviorStack.h"

#include "clad/externalInterface/messageEngineToGame.h"
#include "coretech/common/engine/utils/timer.h"
#include "engine/aiComponent/behaviorComponent/asyncMessageGateComponent.h"
#include "engine/aiComponent/behaviorComponent/behaviors/iCozmoBehavior.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/behaviorExternalInterface.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/behaviorEventComponent.h"
#include "engine/aiComponent/behaviorComponent/behaviorTypesWrapper.h"
#include "engine/aiComponent/behaviorComponent/iBehavior.h"
#include "engine/aiComponent/behaviorComponent/stackMonitors/stackCycleMonitor.h"
#include "engine/aiComponent/behaviorComponent/stackMonitors/stackVizMonitor.h"
#include "engine/externalInterface/externalInterface.h"
#include "util/helpers/boundedWhile.h"
#include "util/logging/logging.h"

#define LOG_CHANNEL "BehaviorSystem"

namespace Anki {
namespace Vector {

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorStack::BehaviorStack()
{
  _stackMonitors.emplace_front( std::make_unique<StackCycleMonitor>() );
  if (ANKI_DEV_CHEATS) {
    _stackMonitors.emplace_front( std::make_unique<StackVizMonitor>() );
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorStack::~BehaviorStack()
{
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
std::string BehaviorStack::StackToBehaviorString(std::vector<IBehavior*> stack)
{
  std::stringstream ss;
  int i = 0;
  for (auto* behavior : stack) {
    auto* cozmoBehavior = dynamic_cast<ICozmoBehavior*>(behavior);
    if (cozmoBehavior == nullptr) {
      continue;
    }
    if (i != 0) {
      ss << "/";
    }
    i++;
    ss << BehaviorTypesWrapper::BehaviorIDToString(cozmoBehavior->GetID());
  }

  return ss.str();
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorStack::InitBehaviorStack(IBehavior* baseOfStack, IExternalInterface* externalInterface)
{
  ANKI_VERIFY(_behaviorStack.empty(),
              "BehaviorSystemManager.BehaviorStack.InitBehaviorStack.StackNotEmptyOnInit",
              "");
  _externalInterface = externalInterface;

  StackMetadataEntry rootMetaData;
  rootMetaData.delegates.insert(baseOfStack);
  rootMetaData.RecursivelyGatherLinkedBehaviors(baseOfStack, rootMetaData.linkedActivationScope);

  _stackMetadataMap.insert(std::make_pair(nullptr, std::move(rootMetaData)));

  PrepareDelegatesToEnterScope(nullptr);

  ANKI_VERIFY(baseOfStack->WantsToBeActivated(),
              "BehaviorSystemManager.BehaviorStack.InitConfig.BasebehaviorDoesNotWantToBeActivated",
              "%s",
              baseOfStack->GetDebugLabel().c_str());

  PushOntoStack(baseOfStack);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorStack::ClearStack()
{
  const bool behaviorStackAlreadyEmpty = _behaviorStack.empty();

  const size_t stackSize = _behaviorStack.size();
  for (int i = 0; i + 1 < stackSize; i++) {
    PopStack();
  }

  // Remove the base of the stack
  if(!behaviorStackAlreadyEmpty &&
     ANKI_VERIFY(! _behaviorStack.empty(),
                 "BehaviorStack.ClearStack.StackImproperlyEmpty",
                 "") ) {
    PopStack();
    PrepareDelegatesForRemovalFromStack(nullptr);
  }

  _stackMetadataMap.erase(nullptr);

  DEV_ASSERT(_behaviorStack.empty(), "BehaviorStack.Destructor.NotAllBehaviorsPoppedFromStack");

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorStack::UpdateBehaviorStack(BehaviorExternalInterface& behaviorExternalInterface,
                                        std::vector<ExternalInterface::RobotCompletedAction>& actionsCompletedThisTick,
                                        AsyncMessageGateComponent& asyncMessageGateComp,
                                        std::set<IBehavior*>& tickedInStack)
{
  if(_behaviorStack.size() == 0){
    LOG_WARNING("BehaviorSystemManager.BehaviorStack.UpdateBehaviorStack.NoStackInitialized", "");
    return;
  }

  // The stack can be altered during update ticks through the cancel delegation
  // functions - so track the index in the stack rather than the iterator directly
  // One side effect of this is that if a Behavior ends this tick without queueing
  // an action we potentially lose a tick before the next time a BSBehavior queues
  // an action - to save on complexity we're accepting this tradeoff for the time being
  // but may decide to address it directly here or within the BSBehavior/one of its subclasses
  // in the future
  auto & eventComponent = behaviorExternalInterface.GetBehaviorEventComponent();

  eventComponent._actionsCompletedThisTick.clear();

  for (int idx = 0; idx < _behaviorStack.size(); idx++) {

    tickedInStack.insert(_behaviorStack.at(idx));

    eventComponent._gameToEngineEvents.clear();
    eventComponent._engineToGameEvents.clear();
    eventComponent._robotToEngineEvents.clear();
    eventComponent._appToEngineEvents.clear();

    asyncMessageGateComp.GetEventsForBehavior(_behaviorStack.at(idx), eventComponent._gameToEngineEvents);
    asyncMessageGateComp.GetEventsForBehavior(_behaviorStack.at(idx), eventComponent._engineToGameEvents);
    asyncMessageGateComp.GetEventsForBehavior(_behaviorStack.at(idx), eventComponent._robotToEngineEvents);
    asyncMessageGateComp.GetEventsForBehavior(_behaviorStack.at(idx), eventComponent._appToEngineEvents);

    // Set the actions completed this tick for the top of the stack
    if(idx == (_behaviorStack.size() - 1)){
      eventComponent._actionsCompletedThisTick = actionsCompletedThisTick;
    }

    _behaviorStack.at(idx)->Update();
  }


  if( _behaviorStackDirty ) {
    NotifyOfChange( behaviorExternalInterface );
    _behaviorStackDirty = false;
  }

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
const IBehavior* BehaviorStack::GetBehaviorInStackAbove(const IBehavior* behavior) const
{
  const auto it = _stackMetadataMap.find(behavior);
  if( it != _stackMetadataMap.end() ) {
    const int idxAbove = it->second.indexInStack + 1;
    if( _behaviorStack.size() > idxAbove ) {
      return _behaviorStack[idxAbove];
    }
  }

  return nullptr;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
const IBehavior* BehaviorStack::GetBehaviorInStackBelow(const IBehavior* behavior) const
{
  const auto it = _stackMetadataMap.find(behavior);
  if( it != _stackMetadataMap.end() ) {
    const int idxBelow = it->second.indexInStack - 1;
    if( 0 <= idxBelow ) {
      return _behaviorStack[idxBelow];
    }
  }

  return nullptr;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorStack::PushOntoStack(IBehavior* behavior)
{
  StackMetadataEntry metadata(behavior, static_cast<int>(_behaviorStack.size()));
  _stackMetadataMap.insert(std::make_pair(behavior, std::move(metadata)));
  _behaviorStack.push_back(behavior);

  PrepareDelegatesToEnterScope(behavior);
  BroadcastAudioBranch(true);
  behavior->OnActivated();

  _behaviorStackDirty = true;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorStack::PopStack()
{
  PrepareDelegatesForRemovalFromStack(_behaviorStack.back());
  BroadcastAudioBranch(false);

  _behaviorStack.back()->OnDeactivated();

  _stackMetadataMap.erase(_behaviorStack.back());
  _behaviorStack.pop_back();

  _behaviorStackDirty = true;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
std::set<IBehavior*> BehaviorStack::GetBehaviorsInActivatableScope()
{
  std::set<IBehavior*> activatableScope;
  // Add all delegates/linked scope
  for (auto& entry: _stackMetadataMap) {
    for (auto& behavior: entry.second.delegates) {
      activatableScope.insert(behavior);
    }
    for (auto& behavior: entry.second.linkedActivationScope) {
      activatableScope.insert(behavior);
    }
  }

  for (auto& behavior : _behaviorStack) {
    auto iter = activatableScope.find(behavior);
    if (iter != activatableScope.end()) {
      activatableScope.erase(iter);
    }
  }

  return activatableScope;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorStack::IsValidDelegation(IBehavior* delegator, IBehavior* delegated)
{
  auto iter = _stackMetadataMap.find(delegator);
  if (iter != _stackMetadataMap.end()) {
    return (iter->second.delegates.find(delegated) != iter->second.delegates.end());
  }
  return false;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorStack::BroadcastAudioBranch(bool activated)
{
  if(ANKI_VERIFY(_externalInterface != nullptr,
                 "BehaviorStack.BroadcastAudioBranch.NoExternalInterface",
                 "")){
    // Create path of BehaviorIds to broadcast
    std::vector<BehaviorID> path;
    path.reserve(_behaviorStack.size());
    for(auto* behavior : _behaviorStack){
      auto* cozmoBehavior = dynamic_cast<ICozmoBehavior*>(behavior);
      if(cozmoBehavior == nullptr){
        continue;
      }
      path.push_back(cozmoBehavior->GetID());
    }
    // Broadcast message
    using namespace ExternalInterface;
    auto state = activated ? BehaviorStackState::Active : BehaviorStackState::NotActive;
    _externalInterface->Broadcast(MessageEngineToGame( AudioBehaviorStackUpdate( state, std::move(path) ) ));
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorStack::PrepareDelegatesToEnterScope(IBehavior* delegated)
{
  for(auto& entry: _stackMetadataMap.find(delegated)->second.delegates){
    entry->OnEnteredActivatableScope();
  }

  for(auto& entry: _stackMetadataMap.find(delegated)->second.linkedActivationScope){
    entry->OnEnteredActivatableScope();
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorStack::PrepareDelegatesForRemovalFromStack(IBehavior* delegated)
{
  auto iter = _stackMetadataMap.find(delegated);
  DEV_ASSERT(iter != _stackMetadataMap.end(),
             "BehaviorStack.PrepareDelegateForRemovalFromStack.DelegateNotFound");
  for (auto& entry: iter->second.delegates) {
    entry->OnLeftActivatableScope();
  }
  for (auto& entry: iter->second.linkedActivationScope) {
    entry->OnLeftActivatableScope();
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorStack::DebugPrintStack(const std::string& debugStr) const
{
#if ALLOW_DEBUG_LOGGING
  const std::string & debugName = "BehaviorSystemManager.Stack." + debugStr;
  for (size_t i=0; i<_behaviorStack.size(); ++i) {
    LOG_DEBUG("BehaviorSystem", debugName.c_str(),
              "%zu: %s",
              i,
              _behaviorStack[i]->GetDebugLabel().c_str());
  }
#endif
}



// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorStack::NotifyOfChange(BehaviorExternalInterface& bei)
{
  for (auto& monitor : _stackMonitors) {
    monitor->NotifyOfChange( bei, _behaviorStack, this );
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Json::Value BehaviorStack::BuildDebugBehaviorTree(BehaviorExternalInterface& behaviorExternalInterface) const
{

  Json::Value data;
  data["time"] = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
  auto& tree = data["tree"] = Json::arrayValue;
  auto& stack = data["stack"] = Json::arrayValue;

  // construct flat table of tree relationships
  for (const auto& elem : _stackMetadataMap) {
    if( elem.first == nullptr )  {
      // skip root node
      continue;
    }
    for (const auto& child : elem.second.delegates) {
      Json::Value relationship;
      relationship["behaviorID"] = child->GetDebugLabel();
      relationship["parent"] = elem.first->GetDebugLabel();
      tree.append( relationship );
    }
  }
  if (!_behaviorStack.empty()) {
    Json::Value relationship;
    relationship["behaviorID"] = _behaviorStack.front()->GetDebugLabel();
    relationship["parent"] = Json::nullValue;
    tree.append( relationship );
  }
  for (const auto& stackElem : _behaviorStack) {
    stack.append( stackElem->GetDebugLabel() );
  }

  return data;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorStack::StackMetadataEntry::StackMetadataEntry(IBehavior* behavior, int indexInStack)
: behavior(behavior)
, indexInStack(indexInStack)
{
  if (behavior != nullptr) {
    behavior->GetAllDelegates(delegates);
    for (auto& delegate: delegates) {
      if (ANKI_VERIFY(delegate != nullptr,
                      "BehaviorStack.DelegateTree.NullDelegate",
                      "Behavior '%s' claims that it may delegate to null",
                      behavior->GetDebugLabel().c_str())) {
        RecursivelyGatherLinkedBehaviors(delegate, linkedActivationScope);
      }
    }
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorStack::StackMetadataEntry::RecursivelyGatherLinkedBehaviors(IBehavior* baseBehavior,
                                                                         std::set<IBehavior*>& linkedBehaviors)
{
  std::set<IBehavior*> rawBehaviors;
  baseBehavior->GetLinkedActivatableScopeBehaviors(rawBehaviors);
  BOUNDED_WHILE(1000, !rawBehaviors.empty()){
    auto bIter = rawBehaviors.begin();
    auto res = linkedBehaviors.insert(*bIter);
    // If insertion was successful this is a new behavior that needs link checking
    if (res.second) {
      // Anki dev cheats - make sure no one erases behavior set passed in existing behaviors
      if (ANKI_DEV_CHEATS) {
        std::set<IBehavior*> dupSet = rawBehaviors;
        (*bIter)->GetLinkedActivatableScopeBehaviors(dupSet);
        for (auto& rBehavior : rawBehaviors) {
          ANKI_VERIFY(dupSet.find(rBehavior) != dupSet.end(),
                      "BehaviorStack.RecursivelyGatherLinkedBehaviors.BehaviorErasedLinkedSet",
                      "Behavior %s erased the behavior set when asked for linked behaviors",
                      (*bIter)->GetDebugLabel().c_str());
        }
      }

      (*bIter)->GetLinkedActivatableScopeBehaviors(rawBehaviors);
    }
    rawBehaviors.erase(bIter);
  }
}


} // namespace Vector
} // namespace Anki

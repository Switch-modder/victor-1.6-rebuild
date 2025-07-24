/**
 * File: behaviorContainer
 *
 * Author: Mark Wesley
 * Created: 11/20/15
 *
 * Description: Container which creates and stores behaviors by ID
 * which were generated from data
 *
 * Copyright: Anki, Inc. 2015
 *
 **/


#include "engine/aiComponent/behaviorComponent/behaviorContainer.h"

#include "clad/externalInterface/messageGameToEngine.h"
#include "clad/types/behaviorComponent/behaviorClasses.h"
#include "clad/types/behaviorComponent/behaviorIDs.h"

#include "coretech/common/engine/jsonTools.h"

#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/behaviorExternalInterface.h"
#include "engine/aiComponent/behaviorComponent/behaviorFactory.h"
#include "engine/aiComponent/behaviorComponent/behaviors/iCozmoBehavior.h"


#define LOG_CHANNEL    "Behaviors"

namespace Anki {
namespace Vector {
  
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorContainer::BehaviorContainer(const BehaviorIDJsonMap& behaviorData)
: IDependencyManagedComponent<BCComponentID>(this, BCComponentID::BehaviorContainer)
{
  for( const auto& behaviorIDJsonPair : behaviorData )
  {
    const auto& behaviorID = behaviorIDJsonPair.first;
    const auto& behaviorJson = behaviorIDJsonPair.second;
    if (!behaviorJson.empty())
    {
      // PRINT_NAMED_DEBUG("BehaviorContainer.Constructor", "Loading '%s'", fullFileName.c_str());
      const bool createdOK = CreateAndStoreBehavior(behaviorJson);
      if ( !createdOK ) {
        PRINT_NAMED_ERROR("Robot.LoadBehavior.CreateFailed",
                          "Failed to create a behavior for behavior id '%s'",
                          BehaviorIDToString(behaviorID));
      }
    }
    else
    {
      PRINT_NAMED_WARNING("Robot.LoadBehavior",
                          "Failed to read behavior file for behavior id '%s'",
                          BehaviorIDToString(behaviorID));
    }
    // don't print anything if we read an empty json
  }
}


  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorContainer::~BehaviorContainer()
{
  // Delete all behaviors owned by the factory
  _idToBehaviorMap.clear();
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorContainer::InitDependent(Robot* robot, const BCCompMap& dependentComps)
{
  auto& bei = dependentComps.GetComponent<BehaviorExternalInterface>();
  Init(bei);
}



// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorContainer::Init(BehaviorExternalInterface& behaviorExternalInterface)
{
  for(auto& behaviorMap: _idToBehaviorMap){
    behaviorMap.second->Init(behaviorExternalInterface);
  }

  for(auto& behaviorMap: _idToBehaviorMap){
    behaviorMap.second->InitBehaviorOperationModifiers();
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
ICozmoBehaviorPtr BehaviorContainer::FindBehaviorByID(BehaviorID behaviorID) const
{
  ICozmoBehaviorPtr foundBehavior = nullptr;
  
  auto scoredIt = _idToBehaviorMap.find(behaviorID);
  if (scoredIt != _idToBehaviorMap.end())
  {
    foundBehavior = scoredIt->second;
    return foundBehavior;
  }
  
  return nullptr;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
std::set<ICozmoBehaviorPtr> BehaviorContainer::FindBehaviorsByClass(BehaviorClass behaviorClass) const
{
  std::set<ICozmoBehaviorPtr> behaviorList;
  for (const auto & behavior : _idToBehaviorMap)
  {
    if( GetBehaviorClass(behavior.second) == behaviorClass )
    {
      behaviorList.insert(behavior.second);
    }
  }
  return behaviorList;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorContainer::CreateAndStoreBehavior(const Json::Value& behaviorConfig)
{
  ICozmoBehaviorPtr newBehavior = BehaviorFactory::CreateBehavior(behaviorConfig);
  if( newBehavior ) {
    const BehaviorID behaviorID = newBehavior->GetID();
    const auto newEntry = _idToBehaviorMap.emplace( behaviorID, newBehavior );
    const bool addedNewEntry = newEntry.second;

    if (addedNewEntry) {
      // PRINT_CH_DEBUG(LOG_CHANNEL, "BehaviorContainer::AddToContainer",
      //                "Added new behavior '%s' %p",
      //                BehaviorIDToString(behaviorID), newBehavior.get());
      return true;
    }
    else {
      DEV_ASSERT_MSG(false,
                     "BehaviorContainer.AddToContainer.DuplicateID",
                     "Attempted to create a second behavior with id %s",
                     newBehavior->GetDebugLabel().c_str());
      return false;
    }
  }
  else {
    return false;
  }
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorContainer::RemoveBehaviorFromMap(ICozmoBehaviorPtr behavior)
{
  // check the scored behavior map
  const auto& scoredIt = _idToBehaviorMap.find(behavior->GetID());
  if (scoredIt != _idToBehaviorMap.end())
  {
    // check it's the same pointer
    ICozmoBehaviorPtr existingBehavior = scoredIt->second;
    if (existingBehavior == behavior)
    {
      _idToBehaviorMap.erase(scoredIt);
      return true;
    }
  }
  
  return false;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorClass BehaviorContainer::GetBehaviorClass(ICozmoBehaviorPtr behavior) const
{
  return behavior->GetClass();
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
std::string BehaviorContainer::GetClassString(BehaviorClass behaviorClass) const
{
  return BehaviorClassToString(behaviorClass);
}
  
} // namespace Vector
} // namespace Anki


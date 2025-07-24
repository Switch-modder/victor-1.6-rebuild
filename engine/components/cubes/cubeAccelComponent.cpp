/**
 * File: cubeAccelComponent.cpp
 *
 * Author: Al Chaussee
 * Created: 04/10/2017
 *
 * Description: Manages streamed object accel data
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#include "engine/components/cubes/cubeAccelComponent.h"

#include "engine/ankiEventUtil.h"
#include "engine/block.h"
#include "engine/blockWorld/blockWorld.h"
#include "engine/components/blockTapFilterComponent.h"
#include "engine/components/carryingComponent.h"
#include "engine/components/cubes/cubeAccelListeners/movementStartStopListener.h"
#include "engine/components/cubes/cubeAccelListeners/upAxisChangedListener.h"
#include "engine/components/cubes/cubeCommsComponent.h"
#include "engine/components/dockingComponent.h"
#include "engine/cozmoContext.h"
#include "engine/robot.h"

#include "clad/externalInterface/messageCubeToEngine.h"

#include "coretech/common/engine/utils/timer.h"

#include "util/console/consoleInterface.h"
#include "util/logging/logging.h"

#define LOG_CHANNEL "CubeAccelComponent"

namespace Anki {
namespace Vector {
  
CubeAccelComponent::CubeAccelComponent()
: IDependencyManagedComponent(this, RobotComponentID::CubeAccel)
{
}


void CubeAccelComponent::InitDependent(Vector::Robot* robot, const RobotCompMap& dependentComps) 
{
  _robot = robot;
  // Subscribe to messages
  if( _robot->HasExternalInterface() ) {
    auto helper = MakeAnkiEventUtil(*_robot->GetExternalInterface(), *this, _eventHandlers);
    using namespace ExternalInterface;
    helper.SubscribeEngineToGame<MessageEngineToGameTag::ObjectConnectionState>();
  }
}


void CubeAccelComponent::UpdateDependent(const RobotCompMap& dependentComps)
{
  // Check to see if anyone is using the listeners, and if not, remove them.
  for (auto& mapEntry : _listenerMap) {
    const auto objId = mapEntry.first;
    auto& listenerSet = mapEntry.second;
    for (auto iter = listenerSet.begin() ; iter != listenerSet.end() ; ) {
      if (iter->use_count() <= 1) {
        LOG_INFO("CubeAccelComponent.UpdateDependent.RemovingListener",
                 "Removing listener for objectID %d since no one is using it",
                 objId.GetValue());
        iter = listenerSet.erase(iter);
      } else {
        ++iter;
      }
    }
  }
}


bool CubeAccelComponent::AddListener(const ObjectID& objectID,
                                     const std::shared_ptr<CubeAccelListeners::ICubeAccelListener>& listener)
{
  const auto* obj = _robot->GetBlockWorld().GetConnectedBlockByID(objectID);
  if (obj == nullptr) {
    PRINT_NAMED_WARNING("CubeAccelComponent.AddListener.InvalidObject",
                        "Object id %d is not connected",
                        objectID.GetValue());
    return false;
  }
  
  // Add this listener to our set of listeners for this object
  auto& listenersSet = _listenerMap[objectID];
  const auto resultPair = listenersSet.insert(listener);
  const bool successfullyInserted = resultPair.second;
  if (!successfullyInserted) {
    PRINT_NAMED_WARNING("CubeAccelComponent.AddListener.InsertFailed",
                        "Failed to insert listener for object %d. Does this listener already exist in the set?",
                        objectID.GetValue());
  }
  return successfullyInserted;
}


void CubeAccelComponent::HandleCubeAccelData(const ActiveID& activeID,
                                             const CubeAccelData& accelData)
{
  auto* object = _robot->GetBlockWorld().GetConnectedBlockByActiveID(activeID);
  if (object == nullptr) {
    DEV_ASSERT(false, "CubeAccelComponent.HandleCubeAccelData.NoConnectedObject");
    return;
  }
  
  const uint32_t objectID = object->GetID();
  
  // Check for taps
  const auto prevTapCnt = object->GetTapCount();
  const auto currTapCnt = accelData.tap_count;
  if (prevTapCnt != currTapCnt) {
    object->SetTapCount(currTapCnt);
    
    if (prevTapCnt != Block::kInvalidTapCnt) {
      ExternalInterface::ObjectTapped objectTapped;
      objectTapped.timestamp = (TimeStamp_t)_robot->GetLastMsgTimestamp();
      objectTapped.objectID  = objectID;
      
      // Pass to BlockTapFilterComponent
      _robot->GetBlockTapFilter().HandleObjectTapped(objectTapped);
    }
  }
  
  for (const auto& accelReading : accelData.accelReadings) {
    // Convert raw accelerometer data to mm/s^2
    auto rawAccelToMmps = [](const s16 rawAccel) {
      // Raw accel is an s16 with range -4g to +4g
      const float accelG = static_cast<float>(rawAccel) / std::numeric_limits<s16>::max() * 4.f;
      return accelG * 9810.f;
    };
    ActiveAccel accel;
    accel.x = rawAccelToMmps(accelReading.accel[0]);
    accel.y = rawAccelToMmps(accelReading.accel[1]);
    accel.z = rawAccelToMmps(accelReading.accel[2]);
    
    const auto& iter = _listenerMap.find(objectID);

    if(iter != _listenerMap.end()) {
      // Update all of the listeners with the accel data
      for(auto& listener : iter->second) {
        listener->Update(accel);
      }
    }
  }
}
  
  
template<>
void CubeAccelComponent::HandleMessage(const ExternalInterface::ObjectConnectionState& msg)
{
  const auto objectId = msg.objectID;
  
  if (msg.connected) {
    using namespace ExternalInterface;
    // Create a listener that detects when the cube starts/stops moving
    auto startedMovingCallback = [this, objectId]() {
      ObjectMovedOrStoppedCallback(objectId, true);
    };
    auto stoppedMovingCallback = [this, objectId]() {
      ObjectMovedOrStoppedCallback(objectId, false);
    };
    auto movedOrStoppedListener = std::make_shared<CubeAccelListeners::MovementStartStopListener>(startedMovingCallback,
                                                                                                  stoppedMovingCallback);
    _movementListeners[objectId] = movedOrStoppedListener;
    AddListener(objectId, movedOrStoppedListener);

    // Create a listener that emits ObjectUpAxisChanged messages
    auto upAxisChangedCallback = [this, objectId](const UpAxis& upAxis) {
      ObjectUpAxisChangedCallback(objectId, upAxis);
    };
    auto upAxisListener = std::make_shared<CubeAccelListeners::UpAxisChangedListener>(upAxisChangedCallback);
    _upAxisChangedListeners[objectId] = upAxisListener;
    AddListener(objectId, upAxisListener);
  } else {
    if (_movementListeners.find(objectId) != _movementListeners.end()) {
      _movementListeners.erase(objectId);
    }
    if (_upAxisChangedListeners.find(objectId) != _upAxisChangedListeners.end()) {
      _upAxisChangedListeners.erase(objectId);
    }
  }
}

void CubeAccelComponent::ObjectMovedOrStoppedCallback(const ObjectID objectId, const bool isMoving)
{
  const RobotTimeStamp_t timestamp = _robot->GetLastMsgTimestamp();
  
  // find active object by objectId
  auto* connectedObj = _robot->GetBlockWorld().GetConnectedBlockByID(objectId);
  if (nullptr == connectedObj) {
    LOG_WARNING("CubeAccelComponent.ObjectMovedOrStoppedCallback.NullObject",
                "Could not find match for object ID %d", objectId.GetValue());
  }
  else
  {
    DEV_ASSERT(connectedObj->IsActive(), "CubeAccelComponent.ObjectMovedOrStoppedCallback.NonActiveObject");
    
    LOG_INFO("CubeAccelComponent.ObjectMovedOrStoppedCallback.ObjectMovedOrStopped",
             "Cube %s, ObjectID: %d (Active ID %d), type: %s",
             isMoving ? "STARTED MOVING" : "STOPPED MOVING",
             connectedObj->GetID().GetValue(), connectedObj->GetActiveID(),
             EnumToString(connectedObj->GetType()));
    
    const bool shouldIgnoreMovement = _robot->GetBlockTapFilter().ShouldIgnoreMovementDueToDoubleTap(connectedObj->GetID());
    if (shouldIgnoreMovement && isMoving)
    {
      LOG_INFO("CubeAccelComponent.ObjectMovedOrStoppedCallback.IgnoringMessage",
               "Waiting for double tap id:%d ignoring movement message",
               connectedObj->GetID().GetValue());
      return;
    }
    
    // update Moving flag of connected object when it changes
    const bool wasMoving = connectedObj->IsMoving();
    if (wasMoving != isMoving) {
      connectedObj->SetIsMoving(isMoving, timestamp);
    }
  }
  
  // Update located instance (if there is one)
  auto* locatedObject = _robot->GetBlockWorld().GetLocatedObjectByID(objectId);
  const bool isCarryingObject = _robot->GetCarryingComponent().IsCarryingObject(objectId);
  if (locatedObject != nullptr) {
    // We expect carried objects to move, so don't mark them as dirty/inaccurate.
    // Their pose state should remain accurate/known because they are attached to
    // the lift. I'm leaving this a separate check from the decision about broadcasting
    // the movement, in case we want to easily remove the checks above but keep this one.
    if (locatedObject->IsPoseStateKnown() && !isCarryingObject)
    {
      // Once an object moves, we can no longer use it for localization because
      // we don't know where it is anymore. Next time we see it, relocalize it
      // relative to robot's pose estimate. Then we can use it for localization
      // again.
      _robot->GetBlockWorld().MarkObjectDirty(locatedObject);
    }
    
    const bool wasMoving = locatedObject->IsMoving();
    if (wasMoving != isMoving) {
      // Set moving state of object (in any frame)
      locatedObject->SetIsMoving(isMoving, timestamp);
    }
  }
  
  // Don't notify game about objects being carried that have moved, since we expect
  // them to move when the robot does.
  // TODO: Consider broadcasting carried object movement if the robot is _not_ moving
  //
  // Don't notify game about moving objects that are being docked with, because
  // we expect those to move if/when we bump them. But we still mark them as dirty/inaccurate
  // below because they have in fact moved and we wouldn't want to localize with them.
  //
  // TODO: Consider not filtering these out and letting game ignore them somehow
  //       - Option 1: give game access to dockingID so it can do this same filtering
  //       - Option 2: add a "wasDocking" flag to the ObjectMoved/Stopped message
  //       - Option 3: add a new ObjectMovedWhileDocking message
  //
  
  const bool isDockingObject = (connectedObj->GetID() == _robot->GetDockingComponent().GetDockObject());
  
  if (!isDockingObject && !isCarryingObject) {
    if (isMoving) {
      using namespace ExternalInterface;
      ObjectMoved objectMoved;
      objectMoved.objectID = objectId;
      objectMoved.timestamp = (TimeStamp_t)timestamp;
      _robot->Broadcast(MessageEngineToGame(std::move(objectMoved)));
    } else {
      using namespace ExternalInterface;
      ObjectStoppedMoving objectStopped;
      objectStopped.objectID = objectId;
      objectStopped.timestamp = (TimeStamp_t)timestamp;
      _robot->Broadcast(MessageEngineToGame(std::move(objectStopped)));
    }
  }
}


void CubeAccelComponent::ObjectUpAxisChangedCallback(const ObjectID objectId, const UpAxis& upAxis)
{
  LOG_INFO("CubeAccelComponent.ObjectUpAxisChangedCallback.UpAxisChanged",
           "ObjectID: %d, UpAxis: %s",
           objectId.GetValue(),
           EnumToString(upAxis));
  
  // Broadcast message
  using namespace ExternalInterface;
  ObjectUpAxisChanged objectUpAxisChanged;
  objectUpAxisChanged.objectID = objectId;
  objectUpAxisChanged.timestamp = (TimeStamp_t) _robot->GetLastMsgTimestamp();
  objectUpAxisChanged.upAxis = upAxis;
  _robot->Broadcast(MessageEngineToGame(std::move(objectUpAxisChanged)));
}


}
}

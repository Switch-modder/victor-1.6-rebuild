/**
 * File: cubePlacementHelper.h
 *
 * Author: ross made the file / original by ??
 * Created: 2018
 *
 * Description: Test helper to place cubes
 *
 * Copyright: Anki, Inc. 2018
 *
 **/



#ifndef __Test_Helpers_CubePlacementHelper_H__
#define __Test_Helpers_CubePlacementHelper_H__
#pragma once

#include "coretech/common/engine/utils/timer.h"
#include "engine/block.h"
#include "engine/blockWorld/blockWorld.h"
#include "engine/blockWorld/blockWorldFilter.h"
#include "util/logging/logging.h"


namespace Anki {
namespace Vector {

class CubePlacementHelper
{
public:

  // Helpers can't have TEST assertions
  static ObservableObject* CreateObjectLocatedAtOrigin(Robot& robot, ObjectType objectType)
  {
    BlockWorld& blockWorld = robot.GetBlockWorld();
    ObservableObject* objectPtr = new Block(objectType);
    objectPtr->SetLastObservedTime( (TimeStamp_t)robot.GetLastMsgTimestamp() );
    
    // check it currently doesn't exist in BlockWorld
    {
      BlockWorldFilter filter;
      filter.SetAllowedTypes( {objectPtr->GetType()} );
      ObservableObject* sameBlock = blockWorld.FindLocatedMatchingObject(filter);
      DEV_ASSERT(nullptr == sameBlock, "CreateObjectLocatedAtOrigin.TypeAlreadyInUse");
    }
    
    // set initial pose & ID (that's responsibility of the system creating the object)
    DEV_ASSERT(!objectPtr->GetID().IsSet(), "CreateObjectLocatedAtOrigin.IDSet");
    DEV_ASSERT(!objectPtr->HasValidPose(), "CreateObjectLocatedAtOrigin.HasValidPose");
    objectPtr->SetID();
    Anki::Pose3d originPose;
    originPose.SetParent( robot.GetWorldOrigin() ); 
    objectPtr->InitPose( originPose, Anki::PoseState::Known); // posestate could be something else
    
    // now they can be added to the world
    blockWorld.AddLocatedObject(std::shared_ptr<ObservableObject>(objectPtr));

    // verify they are there now
    DEV_ASSERT(objectPtr->GetID().IsSet(), "CreateObjectLocatedAtOrigin.IDNotset");
    DEV_ASSERT(objectPtr->HasValidPose(), "CreateObjectLocatedAtOrigin.InvalidPose");
    ObservableObject* objectInWorld = blockWorld.GetLocatedObjectByID( objectPtr->GetID() );
    DEV_ASSERT(objectInWorld == objectPtr, "CreateObjectLocatedAtOrigin.NotSameObject");
    DEV_ASSERT(nullptr != objectInWorld, "CreateObjectLocatedAtOrigin.NullWorldPointer");
    return objectInWorld;
  }
};

}
}

#endif


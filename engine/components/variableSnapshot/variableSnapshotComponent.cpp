/*
 * File: variableSnapshotComponent.cpp
 *
 * Author: Hamzah Khan
 * Created: 5/31/2018
 *
 * Description: This is a component meant to provide persistence across boots for data within other 
 *              components (like timers or faces) that should be remembered by the robot.
 *
 *
 * Copyright: Anki, Inc. 2018
 */

#include "engine/components/variableSnapshot/variableSnapshotComponent.h"

#include "clad/types/variableSnapshotIds.h"


#ifndef __Cozmo_Basestation_VariableSnapshotComponent__
#define __Cozmo_Basestation_VariableSnapshotComponent__

namespace Anki {
namespace Vector {

// save location for data
const char* VariableSnapshotComponent::kVariableSnapshotFolder = "variableSnapshotStorage";
const char* VariableSnapshotComponent::kVariableSnapshotFilename = "variableSnapshot";


VariableSnapshotComponent::VariableSnapshotComponent():
  IDependencyManagedComponent<RobotComponentID>(this, RobotComponentID::VariableSnapshotComponent),
  _variableSnapshotJsonMap(nullptr),
  _robot(nullptr) 
{
}

VariableSnapshotComponent::~VariableSnapshotComponent() 
{
  // upon destruction, save all data
  SaveVariableSnapshots();
}

void VariableSnapshotComponent::InitDependent(Vector::Robot* robot, const RobotCompMap& dependentComponents)
{
  _robot = robot;
  _variableSnapshotJsonMap = _robot->GetDataAccessorComponent().GetVariableSnapshotJsonMap();
}


std::string VariableSnapshotComponent::GetSavePath(const Util::Data::DataPlatform* platform, 
                                                   std::string folderName, 
                                                   std::string filename) 
{
  // cache the name of our save directory
  std::string saveFolder = platform->pathToResource( Util::Data::Scope::Persistent, folderName );
  saveFolder = Util::FileUtils::AddTrailingFileSeparator( saveFolder );

  // make sure our folder structure exists
  if(Util::FileUtils::DirectoryDoesNotExist( saveFolder )) {
    Util::FileUtils::CreateDirectory( saveFolder, false, true );
    PRINT_CH_DEBUG( "DataLoader", "VariableSnapshot", "Creating variable snapshot directory: %s", saveFolder.c_str() );
  }
  
  // read in our data
  std::string variableSnapshotSavePath = ( saveFolder + filename + ".json" );

  if(!Util::FileUtils::FileExists( variableSnapshotSavePath )) {
    PRINT_CH_DEBUG( "DataLoader", "VariableSnapshot", "Creating variable snapshot file: %s", variableSnapshotSavePath.c_str() );
    Json::Value emptyJson;
    platform->writeAsJson(variableSnapshotSavePath, emptyJson);
  }

  return variableSnapshotSavePath;
}


bool VariableSnapshotComponent::SaveVariableSnapshots()
{
  auto platform = _robot->GetContextDataPlatform();

  // update the stored json data
  for(const auto& dataMapIter : _variableSnapshotDataMap) {
    Json::Value outJson;
    VariableSnapshotId variableSnapshotId = dataMapIter.first;
    
    // dataMapIter.second is the serialization function identified and stored by InitVariable for this data
    dataMapIter.second(outJson);
    outJson[VariableSnapshotEncoder::kVariableSnapshotIdKey] = VariableSnapshotIdToString(variableSnapshotId);
    (*_variableSnapshotJsonMap)[variableSnapshotId] = std::move(outJson);
  }

  // create a json list that will be stored
  Json::Value saveJson;
  for(const auto& subscriber : *_variableSnapshotJsonMap) {
    saveJson.append(subscriber.second);
  }
  
  std::string path = VariableSnapshotComponent::GetSavePath(platform, kVariableSnapshotFolder, kVariableSnapshotFilename);
  const bool success = platform->writeAsJson(path, saveJson);
  return success;
}


} // Cozmo
} // Anki

#endif

/**
 * File: cubeLightAnimationContainer.cpp
 *
 * Authors: Al Chaussee
 * Created: 1/23/2017
 *
 * Description: Container for json-defined cube light animations
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#include "engine/components/cubes/cubeLights/cubeLightAnimationContainer.h"

#include "coretech/common/engine/colorRGBA.h"
#include "engine/components/cubes/cubeLights/cubeLightAnimationHelpers.h"
#include "engine/components/cubes/cubeLights/cubeLightComponent.h"
#include "util/fileUtils/fileUtils.h"

namespace Anki {
namespace Vector {
  
CubeLightAnimationContainer::CubeLightAnimationContainer(const InitMap& initializationMap)
{
  for(const auto& pair: initializationMap){
    const bool mustHaveExtension = true;
    const bool removeExtension = true;
    auto animName = Util::FileUtils::GetFileName(pair.first, mustHaveExtension, removeExtension);
    
    CubeLightAnimation::Animation animation;
    CubeLightAnimation::ParseCubeAnimationFromJson(animName, pair.second, animation);
    _animations.emplace(std::move(animName), std::move(animation));
  }
}


CubeLightAnimation::Animation* CubeLightAnimationContainer::GetAnimationHelper(const std::string& name) const
{
  CubeLightAnimation::Animation* animPtr = nullptr;
  
  auto retVal = _animations.find(name);
  if(retVal == _animations.end()) {
    PRINT_NAMED_ERROR("CubeLightAnimationContainer.GetAnimation_Const.InvalidName",
                      "Animation requested for unknown animation '%s'.",
                      name.c_str());
  } else {
    animPtr = const_cast<CubeLightAnimation::Animation*>(&retVal->second);
  }
  
  return animPtr;
}

const CubeLightAnimation::Animation* CubeLightAnimationContainer::GetAnimation(const std::string& name) const
{
  return GetAnimationHelper(name);
}

CubeLightAnimation::Animation* CubeLightAnimationContainer::GetAnimation(const std::string& name)
{
  return GetAnimationHelper(name);
}

  
} // namespace Vector
} // namespace Anki

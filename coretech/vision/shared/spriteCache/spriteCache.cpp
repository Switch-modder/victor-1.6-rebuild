/**
* File: spriteCache.cpp
*
* Author: Kevin M. Karol
* Created: 4/12/2018
*
* Description: Provides a uniform interface for accessing sprites defined on disk
* which can either be cached in memory, read from disk when the request is received
* or otherwise intelligently managed
*
* Copyright: Anki, Inc. 2018
*
**/


#include "coretech/vision/shared/spriteCache/spriteCache.h"

#include "util/logging/logging.h"
#include "util/math/math.h"

namespace Anki {
namespace Vision {

namespace{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
SpriteCache::SpriteCache(const Vision::SpritePathMap* spriteMap)
: _spritePathMap(spriteMap)
{

}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
SpriteCache::~SpriteCache()
{
  
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
SpriteHandle SpriteCache::GetSpriteHandleForNamedSprite(const std::string& spriteName, 
                                                        const HSImageHandle& hueAndSaturation)
{
  std::string fullSpritePath;
  if(_spritePathMap->IsSpriteSequence(spriteName)){
    LOG_ERROR("SpriteCache.GetSpriteHandleForNamedSprite.InvalidSpriteName",
              "Asset name: %s refers to a SpriteSequence, not a sprite. Returning missing sprite asset",
              spriteName.c_str());
    fullSpritePath = _spritePathMap->GetPlaceholderAssetPath();
  }else{
    // NOTE: If there is no sprite for this spriteName, the SpritePathMap will return a path to the 
    //       default missing_sprite asset and it will render in place of the desired sprite
    fullSpritePath = _spritePathMap->GetAssetPath(spriteName);
  }
  return GetSpriteHandleInternal(fullSpritePath, hueAndSaturation);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
SpriteHandle SpriteCache::GetSpriteHandleForSpritePath(const std::string& fullSpritePath, 
                                                       const HSImageHandle& hueAndSaturation)
{
  return GetSpriteHandleInternal(fullSpritePath, hueAndSaturation);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
SpriteCache::InternalHandle SpriteCache::GetSpriteHandleInternal(const std::string& fullSpritePath, 
                                                                 const HSImageHandle& hueAndSaturation)
{
  std::lock_guard<std::mutex> guard(_hueSaturationMapMutex);

  auto& filePathMap = GetHandleMapForHue(hueAndSaturation);

  {
    // See if handle can be returned from the cache
    auto iter = filePathMap.find(fullSpritePath);
    if(iter != filePathMap.end()){
      return iter->second;
    }
  }

  InternalHandle handle = std::make_shared<SpriteWrapper>(fullSpritePath);
  filePathMap.emplace(fullSpritePath, handle);

  return handle;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
SpriteCache::InternalHandle SpriteCache::ConvertToInternalHandle(SpriteHandle handle,
                                                                 const HSImageHandle& hueAndSaturation)
{
  InternalHandle internalHandle;
  {
    std::lock_guard<std::mutex> guard(_hueSaturationMapMutex);
    auto& handleMap = GetHandleMapForHue(hueAndSaturation);

    for(auto& pair : handleMap){
      if(pair.second.get() == handle.get()){
        internalHandle = pair.second;
        break;
      }
    }

  } // guard falls out of scope to allow call to GetSpriteHandleInternal
  
  if(internalHandle == nullptr){
    std::string fullSpritePath;
    if(handle->GetFullSpritePath(fullSpritePath)){
      internalHandle = GetSpriteHandleInternal(fullSpritePath, hueAndSaturation);
    }
  }

  return internalHandle;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
SpriteCache::SpriteNameToHandleMap& SpriteCache::GetHandleMapForHue(const HSImageHandle& hueAndSaturation)
{
  const uint16_t compressedKey = hueAndSaturation != nullptr ? hueAndSaturation->GetHSID() : 0;
  auto iter = _hueSaturationMap.find(compressedKey);
  if(iter == _hueSaturationMap.end()){
    iter = _hueSaturationMap.emplace(compressedKey, SpriteNameToHandleMap()).first;
  }

  return iter->second;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void SpriteCache::Update(BaseStationTime_t currTime_nanosec)
{
  _lastUpdateTime_nanosec = currTime_nanosec;

  auto iter = _cacheTimeoutMap.begin();
  while(iter != _cacheTimeoutMap.end()){
    if(iter->first != 0){
      if(iter->first < _lastUpdateTime_nanosec){
        iter->second->ClearCachedSprite();
        iter = _cacheTimeoutMap.erase(iter);
      }else{
        break;
      }
    }else{
      ++iter;
    }
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void SpriteCache::CacheSprite(const SpriteHandle& handle, 
                              ImgTypeCacheSpec cacheSpec,
                              BaseStationTime_t cacheFor_ms, 
                              const HSImageHandle& hueAndSaturation)
{
  InternalHandle internalHandle = ConvertToInternalHandle(handle, hueAndSaturation);
  
  if(internalHandle != nullptr){
    internalHandle->CacheSprite(cacheSpec, hueAndSaturation);
    const auto expire_ns = _lastUpdateTime_nanosec + Util::MilliSecToNanoSec(cacheFor_ms);
    _cacheTimeoutMap.emplace(expire_ns, internalHandle);
  }else{
    PRINT_NAMED_WARNING("SpriteCache.CacheSprite.UnableToFindSpriteToCache", "");
  }

}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void SpriteCache::ClearCachedSprite(SpriteHandle handle, 
                                    ImgTypeCacheSpec cacheSpec, 
                                    const HSImageHandle& hueAndSaturation)
{
  InternalHandle internalHandle = ConvertToInternalHandle(handle, hueAndSaturation);

  for(auto iter = _cacheTimeoutMap.begin(); iter != _cacheTimeoutMap.end(); ++iter){
    if(iter->second == internalHandle){
      internalHandle->ClearCachedSprite();
      _cacheTimeoutMap.erase(iter);
      break;
    }
  }
}


} // namespace Vision
} // namespace Anki

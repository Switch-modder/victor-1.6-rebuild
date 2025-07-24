/**
 * File: cubeLightComponent.cpp
 *
 * Author: Al Chaussee
 * Created: 12/2/2016
 *
 * Description: Manages playing cube light animations on objects
 *              A cube light animation consists of multiple patterns, where each pattern has a duration and
 *              when the pattern has been playing for that duration the next pattern gets played.
 *              An animation can be played on one of three layers (User/Game, Engine, Default), which
 *              function as a priority system with the User/Game layer having highest priority.
 *              Each layer can have multiple animations "playing" in a stack so once the top level animation
 *              finishes the next anim will resume playing assuming its total duration has not been reached. This
 *              is to support blending animations together (playing an animation on top of another)
 *
 * Copyright: Anki, Inc. 2016
 *
 **/

#include "engine/components/cubes/cubeLights/cubeLightComponent.h"

#include "engine/actions/actionInterface.h"
#include "engine/components/cubes/cubeLights/cubeLightAnimationContainer.h"
#include "engine/components/cubes/cubeLights/cubeLightAnimationHelpers.h"
#include "engine/ankiEventUtil.h"
#include "engine/block.h"
#include "engine/blockWorld/blockWorld.h"
#include "engine/components/carryingComponent.h"
#include "engine/components/cubes/cubeCommsComponent.h"
#include "engine/components/visionComponent.h"
#include "engine/cozmoContext.h"
#include "engine/events/ankiEvent.h"
#include "engine/externalInterface/externalInterface.h"
#include "engine/externalInterface/gatewayInterface.h"
#include "engine/robot.h"
#include "engine/robotDataLoader.h"

#include "anki/cozmo/shared/cozmoConfig.h"

#include "coretech/common/engine/utils/data/dataPlatform.h"
#include "coretech/common/engine/utils/timer.h"

#include "proto/external_interface/messages.pb.h"
#include "proto/external_interface/shared.pb.h"

#include "util/fileUtils/fileUtils.h"

#define DEBUG_TEST_ALL_ANIM_TRIGGERS 0
#define DEBUG_LIGHTS 0

// Scales colors by this factor when applying white balancing
static constexpr f32 kWhiteBalanceScale = 0.6f;

namespace Anki {
namespace Vector {

// Convert MS to LED FRAMES
#define MS_TO_LED_FRAMES(ms)  ((ms) == std::numeric_limits<u32>::max() ?      \
                                std::numeric_limits<u8>::max() :              \
                                Util::numeric_cast<u8>(((ms)+CUBE_LED_FRAME_LENGTH_MS-1)/CUBE_LED_FRAME_LENGTH_MS))
  
bool CubeLightAnimation::ObjectLights::operator==(const CubeLightAnimation::ObjectLights& other) const
{
  return this->onColors        == other.onColors &&
  this->offColors              == other.offColors &&
  this->onPeriod_ms            == other.onPeriod_ms &&
  this->offPeriod_ms           == other.offPeriod_ms &&
  this->transitionOnPeriod_ms  == other.transitionOnPeriod_ms &&
  this->transitionOffPeriod_ms == other.transitionOffPeriod_ms &&
  this->offset                 == other.offset &&
  this->rotate                 == other.rotate &&
  this->makeRelative           == other.makeRelative &&
  this->relativePoint          == other.relativePoint;
}
  

#if ANKI_DEV_CHEATS
void CubeLightComponent::SaveLightsToDisk(const std::string& fileName,
                                          const CubeLightAnimation::Animation& anim)
{
  auto json = CubeLightAnimation::AnimationToJSON(fileName, anim);
  const Util::Data::DataPlatform* platform = _robot->GetContext()->GetDataPlatform();
  auto relFolderPath = "DevLights/";
  auto fullFolderPath = platform->pathToResource( Util::Data::Scope::Cache, relFolderPath );
  fullFolderPath = Util::FileUtils::AddTrailingFileSeparator( fullFolderPath );

  if ( Util::FileUtils::DirectoryDoesNotExist( fullFolderPath ) )
  {
    Util::FileUtils::CreateDirectory( fullFolderPath, false, true );
  }

  platform->writeAsJson(fullFolderPath + fileName,
                        json);
}
#endif

CubeLightComponent::CubeLightComponent()
: IDependencyManagedComponent(this, RobotComponentID::CubeLights)
{
  _appToEngineTags.insert(AppToEngineTag::kSetCubeLightsRequest);

  static_assert(AnimLayerEnum::User < AnimLayerEnum::Engine &&
                AnimLayerEnum::Engine < AnimLayerEnum::State,
                "CubeLightComponent.LayersInUnexpectedOrder");
}


void CubeLightComponent::InitDependent(Vector::Robot* robot, const RobotCompMap& dependentComps)
{
  _robot = robot;
  _cubeLightAnimations = std::make_unique<CubeLightAnimationContainer>(_robot->GetContext()->GetDataLoader()->GetCubeLightAnimations());
  _triggerToAnimMap = robot->GetContext()->GetDataLoader()->GetCubeAnimationTriggerMap();
  // Subscribe to messages
  if( _robot->HasExternalInterface() ) {
    auto helper = MakeAnkiEventUtil(*_robot->GetExternalInterface(), *this, _eventHandles);
    using namespace ExternalInterface;
    helper.SubscribeEngineToGame<MessageEngineToGameTag::ObjectConnectionState>();
    helper.SubscribeEngineToGame<MessageEngineToGameTag::RobotDelocalized>();
    
    helper.SubscribeGameToEngine<MessageGameToEngineTag::PlayCubeAnim>();
    helper.SubscribeGameToEngine<MessageGameToEngineTag::StopCubeAnim>();
    helper.SubscribeGameToEngine<MessageGameToEngineTag::SetAllActiveObjectLEDs>();
    helper.SubscribeGameToEngine<MessageGameToEngineTag::SetActiveObjectLEDs>();
    helper.SubscribeGameToEngine<MessageGameToEngineTag::EnableLightStates>();
  }

  if( robot->HasGatewayInterface() ) {
    _gi = robot->GetGatewayInterface();

    // Subscribe to app->engine tags
    for (const auto& tag : _appToEngineTags) {
      _eventHandles.push_back(_gi->Subscribe(tag, std::bind(&CubeLightComponent::HandleAppRequest,
                                                            this, std::placeholders::_1)));
    }
  }
}


void CubeLightComponent::UpdateDependent(const RobotCompMap& dependentComps)
{
  UpdateInternal(true);
}


void CubeLightComponent::UpdateInternal(bool shouldPickNextAnim)
{
  const EngineTimeStamp_t curTime = BaseStationTimer::getInstance()->GetCurrentTimeStamp();
  
  // We are going from delocalized to localized
  bool doRelocalizedUpdate = false;
  if(_robotDelocalized && _robot->IsLocalized())
  {
    doRelocalizedUpdate = true;
    _robotDelocalized = false;
  }
  
  for(auto& objectInfo : _objectInfo)
  {
    // Pick the correct animation to play when relocalizing
    if(doRelocalizedUpdate)
    {
      PickNextAnimForDefaultLayer(objectInfo.first);
      continue;
    }
  
    AnimLayerEnum& layer = objectInfo.second.curLayer;
    
    // If there are no animations on this layer don't do anything
    if(objectInfo.second.animationsOnLayer[layer].empty())
    {
      continue;
    }
    
    auto& objectAnim = objectInfo.second.animationsOnLayer[layer].back();
    
    // If the current animation never ends and it isn't being stopped don't do anything
    if(objectAnim.timeCurPatternEnds == 0 && !objectAnim.stopNow)
    {
      continue;
    }
    // Otherwise if the current pattern in this animation should end or the whole animation is being stopped
    else if(curTime >= objectAnim.timeCurPatternEnds || objectAnim.stopNow)
    {
      // The current pattern is ending so try to go to the next pattern in the animation
      ++objectAnim.curPattern;
      
      // If there are no more patterns in this animation or the animation is being stopped
      if(objectAnim.curPattern == objectAnim.endOfPattern || objectAnim.stopNow)
      {
        auto removeAndCallback = [&objectAnim, &objectInfo, &layer]()
        {
          auto callback = objectAnim.callback;
          const u32 actionTag = objectAnim.actionTagCallbackIsFrom;
          
          // Remove this animation from the stack
          // Note: objectAnim is now invalid. It can NOT be reassigned or accessed
          objectInfo.second.animationsOnLayer[layer].pop_back();
          
          // Call the callback since the animation is complete
          if(callback)
          {
            // Only call the callback if it isn't from an action or
            // it is from an action and that action's tag is still in use
            // If the action gets destroyed and the tag is no longer in use the callback would be
            // invalid
            if(actionTag == 0 ||
               IActionRunner::IsTagInUse(actionTag))
            {
              callback();
            }
          }
        };
        
        // Note: objectAnim invalidated by this call
        removeAndCallback();
      
        // Keep removing animations and calling callbacks while there are animations to stop
        // on this layer
        while(!objectInfo.second.animationsOnLayer[layer].empty() &&
              objectInfo.second.animationsOnLayer[layer].back().stopNow)
        {
          removeAndCallback();
        }
      
        // If there are no more animations being played on this layer
        if(objectInfo.second.animationsOnLayer[layer].empty())
        {
          // If all layers are enabled go back to default layer
          if(!objectInfo.second.isOnlyGameLayerEnabled)
          {
            const AnimLayerEnum prevLayer = layer;

            layer = AnimLayerEnum::State;
            
            PRINT_CH_INFO("CubeLightComponent", "CubeLightComponent.Update.NoAnimsLeftOnLayer",
                          "No more animations left on layer %s for object %u going to %s layer",
                          LayerToString(prevLayer),
                          objectInfo.first.GetValue(),
                          LayerToString(layer));
            
            // In some cases we don't want to PickNextAnimForDefaultLayer in order to prevent
            // lights from flickering when stopping and playing anims
            if(shouldPickNextAnim)
            {
              PickNextAnimForDefaultLayer(objectInfo.first);
            }
          }
          // Otherwise only game layer is enable so just turn the lights off
          else
          {
            Result res = SetCubeLightAnimation(objectInfo.first, CubeLightAnimation::GetLightsOffObjectLights());
            if(res != RESULT_OK)
            {
              PRINT_NAMED_WARNING("CubeLightComponent.Update.SetLightsFailed", "");
            }
          }
          continue;
        }

        // There are still animations being played on this layer so switch to the next anim
        // Note: A new variable objectAnim2 is created instead of reassigning objectAnim
        // This is because objectAnim has become invalid by the call to pop_back() which means
        // trying to assign to it is not valid
        //
        // std::list<std::string> l; l.push_back("a"); l.push_back("b");
        // std::string& x = l.back(); // reference to "b"
        // l.pop_back(); // removes "b" and invalidates x
        // x = l.back(); // this looks like it should make x reference "a" however x is
        // invalid from the pop_back and cannot be reassigned
        const auto& prevObjectAnim = objectInfo.second.animationsOnLayer[layer].back();
        PRINT_CH_INFO("CubeLightComponent", "CubeLightComponent.Update.PlayingPrevious",
                      "Have previous anim %s on layer %s id %u",
                      prevObjectAnim.debugName.c_str(),
                      LayerToString(layer),
                      objectInfo.first.GetValue());
        
        Result res = SetCubeLightAnimation(objectInfo.first, prevObjectAnim.curPattern->lights);
        if(res != RESULT_OK)
        {
          PRINT_NAMED_WARNING("CubeLightComponent.Update.SetLightsFailed", "");
        }
        continue;
      }
      // Otherwise, we are going to the next pattern in the animation so update the time the pattern will end
      else
      {
        objectAnim.timeCurPatternEnds = 0;
        if(objectAnim.curPattern->duration_ms > 0 ||
           objectAnim.durationModifier_ms != 0)
        {
          objectAnim.timeCurPatternEnds = curTime +
                                          objectAnim.curPattern->duration_ms +
                                          objectAnim.durationModifier_ms;
          
          DEV_ASSERT(objectAnim.timeCurPatternEnds > 0,
                     "CubeLightComponent.Update.TimeCurPatternEndLessThanZero");
        }
      }
      
      // Finally set the lights with the current pattern of the animation
      Result res = SetCubeLightAnimation(objectInfo.first, objectAnim.curPattern->lights);
      if(res != RESULT_OK)
      {
        PRINT_NAMED_WARNING("CubeLightComponent.Update.SetLightsFailed", "");
      }
    }
  }
}

bool CubeLightComponent::PlayLightAnimFromAction(const ObjectID& objectID,
                                                 const CubeAnimationTrigger& animTrigger,
                                                 AnimCompletedCallback callback,
                                                 const u32& actionTag)
{
  const AnimLayerEnum layer = AnimLayerEnum::User;
  const bool res = PlayLightAnimByTrigger(objectID, animTrigger, layer, callback);
  if(res)
  {
    // iter is guaranteed to be valid otherwise PlayLightAnim will have returned false
    // animationOnLayer[layer] is guaranteed to not be empty because PlayLightAnim will return true
    // when an animation is added to the layer
    auto iter = _objectInfo.find(objectID);
    
    DEV_ASSERT(iter != _objectInfo.end(), "CubeLightComponent.PlayLightAnimFromAction.IterNotValid");
    
    // Update the action tag the callback is from, the animation was just added to the end
    // of animationsOnLayer[layer] by the call to PlayLightAnim
    iter->second.animationsOnLayer[layer].back().actionTagCallbackIsFrom = actionTag;
  }
  return res;
}

CubeLightAnimation::Animation* CubeLightComponent::GetAnimation(const CubeAnimationTrigger& animTrigger)
{
  auto animName = _triggerToAnimMap->GetValue(animTrigger);
  return _cubeLightAnimations->GetAnimation(animName);
}

bool CubeLightComponent::PlayLightAnimByTrigger(const ObjectID& objectID,
                                                const CubeAnimationTrigger& animTrigger,
                                                AnimCompletedCallback callback,
                                                bool playDuringBackgroundConnection,
                                                bool hasModifier,
                                                const CubeLightAnimation::ObjectLights& modifier,
                                                const s32 durationModifier_ms)
{
  if(_cubeConnectedInBackground && !playDuringBackgroundConnection){
    return false;
  }
  return PlayLightAnimByTrigger(objectID, animTrigger, AnimLayerEnum::Engine, callback, hasModifier, modifier, durationModifier_ms);
}

bool CubeLightComponent::PlayLightAnimByTrigger(const ObjectID& objectID,
                                                const CubeAnimationTrigger& animTrigger,
                                                const AnimLayerEnum& layer,
                                                AnimCompletedCallback callback,
                                                bool hasModifier,
                                                const CubeLightAnimation::ObjectLights& modifier,
                                                const s32 durationModifier_ms)
{
  // Get the name of the animation to play for this trigger
  const std::string& animName = _robot->GetContext()->GetDataLoader()->GetCubeAnimationForTrigger(animTrigger);
  auto* anim = _cubeLightAnimations->GetAnimation(animName);
  if(anim == nullptr)
  {
    PRINT_NAMED_WARNING("CubeLightComponent.NoAnimForTrigger",
                        "No animation found for trigger %s", animName.c_str());
    return false;
  }
  
  AnimationHandle handle;
  const bool success = PlayLightAnimInternal(objectID, *anim, layer, callback, 
                                             hasModifier, modifier, durationModifier_ms, animName, handle);
  if(success){
    _triggerToHandleMap.emplace(animTrigger, handle);
  }
  return success;
}

bool CubeLightComponent::PlayLightAnim(const ObjectID& objectID,
                                       CubeLightAnimation::Animation& animation,
                                       AnimCompletedCallback callback,
                                       const std::string& debugName,
                                       AnimationHandle& outHandle)
{
  return PlayLightAnimInternal(objectID, animation, AnimLayerEnum::Engine, callback, false, {}, 0, debugName, outHandle);
}

bool CubeLightComponent::PlayLightAnimInternal(const ObjectID& objectID,
                                               CubeLightAnimation::Animation& animation,
                                               const AnimLayerEnum& layer,
                                               AnimCompletedCallback callback,
                                               bool hasModifier,
                                               const CubeLightAnimation::ObjectLights& modifier,
                                               const s32 durationModifier_ms,
                                               const std::string& debugName, 
                                               AnimationHandle& outHandle)
{
  auto iter = _objectInfo.find(objectID);
  if(iter == _objectInfo.end())
  {
    PRINT_CH_INFO("CubeLightComponent", "CubeLightComponent.PlayLightAnim.InvalidObjectID",
                  "No object with id %u exists in _objectInfo map can't play animation",
                  objectID.GetValue());
    return false;
  }

  auto& objectInfo = iter->second;

  auto& animationsOnLayer = objectInfo.animationsOnLayer[layer];
  
  const bool isPlayingAnim = !animationsOnLayer.empty();
  const bool isCurAnimBlended = (isPlayingAnim && animationsOnLayer.back().isBlended);
  
  // If we are currently playing an animation and it can not be overridden do nothing
  if(isPlayingAnim &&
      !animationsOnLayer.back().canBeOverridden)
  {
    PRINT_CH_INFO("CubeLightComponent", "CubeLightComponent.PlayLightAnim.CantBeOverridden",
                  "Current anim %s can not be overridden on object %u layer %s",
                  animationsOnLayer.back().debugName.c_str(),
                  objectID.GetValue(),
                  LayerToString(layer));
    return false;
  }
  

  
  // If only the game layer is enabled and we are trying to play this animation on a different layer
  if(objectInfo.isOnlyGameLayerEnabled &&
      layer != AnimLayerEnum::User)
  {
    PRINT_CH_INFO("CubeLightComponent", "CubeLightComponent.OnlyGameLayerEnabled",
                  "Only game layer is enabled, refusing to play anim %s on object %u layer %s",
                  debugName.c_str(),
                  objectID.GetValue(),
                  LayerToString(layer));
    return false;
  }
  
  // If the game/user layer is not enabled and an animation is trying to be played on the game/user layer
  // don't play it
  if(!objectInfo.isOnlyGameLayerEnabled &&
      layer == AnimLayerEnum::User)
  {
    PRINT_CH_INFO("CubeLightComponent", "CubeLightComponent.PlayLightAnim.NotPlayingUserAnim",
                  "Refusing to play anim on %s layer when it is not enabled",
                  LayerToString(layer));
    return false;
  }
  
  // If we have a modifier to apply to the animation apply it
  CubeLightAnimation::Animation modifiedAnim;
  if(hasModifier)
  {
    ApplyAnimModifier(animation, modifier, modifiedAnim);
  }
  
  // Attempt to blend the animation with the current light pattern. If the animation can not be blended
  // with the current animation then blendedAnim will be the same as either modifiedAnim or animIter->second
  CubeLightAnimation::Animation blendedAnim;
  const bool didBlend = BlendAnimWithCurLights(objectID,
                                                (hasModifier ? modifiedAnim : animation),
                                                blendedAnim);
  
  // If the current animation is already a blended animation and the animation to play was able
  // to be blended with the current animation then do nothing
  // TODO (Al): Maybe support this ability in the future. Animation duration of blended animations gets
  // complicated
  if(isCurAnimBlended && didBlend)
  {
    PRINT_CH_INFO("CubeLightComponent", "CubeLightComponent.PlayLightAnim.AlreadyPlayingBlendedAnim",
                  "Can't play a blended anim over already playing blended anim");
    return false;
  }
  
  CurrentAnimInfo info;
  info.callback = callback;
  info.animationHandle = _nextAnimationHandle;
  outHandle = _nextAnimationHandle;
  _nextAnimationHandle++;
  // Treat modified animations the same as blended animations (ie they live in the blendedAnims array)
  if(!isCurAnimBlended &&
      (didBlend || hasModifier))
  {
    // Store the blendedAnim in the blendedAnims array so we can grab iterators to it
    objectInfo.blendedAnims[layer] = blendedAnim;
    
    info.debugName = (isPlayingAnim ? animationsOnLayer.back().debugName+"+" : "") + debugName;
    info.curPattern = objectInfo.blendedAnims[layer].begin();
    info.endOfPattern = objectInfo.blendedAnims[layer].end();
    info.isBlended = true;
  }
  else
  {
    info.debugName = debugName;
    info.curPattern = animation.begin();
    info.endOfPattern = animation.end();
    
    // If this animation has no duration then clear the existing animations on the layer
    if(info.curPattern->duration_ms == 0)
    {
      animationsOnLayer.clear();
    }
  }
  
  // Add the new animation to the list of animations playing on this layer
  animationsOnLayer.push_back(info);
  
  auto& newAnimOnLayer = animationsOnLayer.back();
  
  const EngineTimeStamp_t curTime = BaseStationTimer::getInstance()->GetCurrentTimeStamp();
  
  // Set this animation's duration modifier
  newAnimOnLayer.durationModifier_ms = durationModifier_ms;
  
  newAnimOnLayer.timeCurPatternEnds = 0;
  
  // If the first pattern in the animation has a non zero duration then update the time the pattern
  // should end
  if(newAnimOnLayer.curPattern->duration_ms > 0 ||
      newAnimOnLayer.durationModifier_ms != 0)
  {
    newAnimOnLayer.timeCurPatternEnds = curTime +
                                    newAnimOnLayer.curPattern->duration_ms +
                                    newAnimOnLayer.durationModifier_ms;
    
    DEV_ASSERT(newAnimOnLayer.timeCurPatternEnds > 0,
                "CubeLightComponent.PlayLightAnim.TimeCurPatternEndLessThanZero");
  }
  
  // Whether or not this animation canBeOverridden is defined by whether or not the last pattern in the
  // animation definition can be overridden
  newAnimOnLayer.canBeOverridden = animation.back().canBeOverridden;

  // If this layer has higher priority then actually set the object lights
  // User > Engine > Default (User layer takes precedence over all layers)
  if(layer <= objectInfo.curLayer)
  {
    PRINT_CH_INFO("CubeLightComponent", "CubeLightComponent.PlayLightAnim.SettingLights",
                  "Playing animation %s on layer %s for object %u",
                  debugName.c_str(),
                  LayerToString(layer),
                  objectID.GetValue());
    
    // Update the current layer for this object
    objectInfo.curLayer = layer;
    
    Result res = SetCubeLightAnimation(objectID, newAnimOnLayer.curPattern->lights);
    if(res != RESULT_OK)
    {
      // Since no light pattern was set, setting the end time of the pattern to zero will make it so that
      // everytime it is updated nothing will happen (previous pattern will probably be playing)
      newAnimOnLayer.timeCurPatternEnds = 0;
    }
  }
  // Otherwise the animation was still added to the layer but a higher priority layer is currently
  // playing an animation so we didn't actually set object lights
  else
  {
    PRINT_CH_INFO("CubeLightComponent", "CubeLightComponent.PlayLightAnim.LightsNotSet",
                  "Lights not set on object %u layer %s because current layer is %u",
                  objectID.GetValue(), LayerToString(layer), objectInfo.curLayer);
  }
  return true;

}

void CubeLightComponent::StopAllAnims()
{
  StopAllAnimsOnLayer(AnimLayerEnum::Engine);
}

void CubeLightComponent::StopAllAnimsOnLayer(const AnimLayerEnum& layer, const ObjectID& objectID)
{
  // If objectID is unknown (-1) then stop all animations for all objects on the given layer
  if(objectID.IsUnknown())
  {
    PRINT_CH_INFO("CubeLightComponent", "CubeLightComponent.StopAllAnimsOnLayer.AllObjects",
                  "Stopping all anims for all objects on layer %s",
                  LayerToString(layer));
    for(auto& pair : _objectInfo)
    {
      for(auto& anim : pair.second.animationsOnLayer[layer])
      {
        anim.stopNow = true;
      }
    }
  }
  else
  {
    auto iter = _objectInfo.find(objectID);
    if(iter == _objectInfo.end())
    {
      PRINT_CH_INFO("CubeLightComponent", "CubeLightComponent.StopAllAnimsOnLayer.BadObject",
                    "ObjectId %u does not exist in _objectInfo map unable to stop anims",
                    objectID.GetValue());
      return;
    }

    for(auto& anim : iter->second.animationsOnLayer[layer])
    {
      anim.stopNow = true;
    }
  }
  
  // Manually update so the anims are immediately stopped
  UpdateInternal(true);
}

bool CubeLightComponent::StopLightAnimAndResumePrevious(const CubeAnimationTrigger& animTrigger,
                                                        const ObjectID& objectID)
{
  return StopLightAnimByTrigger(animTrigger, AnimLayerEnum::Engine, objectID);
}

bool CubeLightComponent::StopLightAnimAndResumePrevious(const AnimationHandle& handle,
                                                        const ObjectID& objectID)
{
  return StopLightAnimInternal(handle, AnimLayerEnum::Engine, objectID);
}

bool CubeLightComponent::StopLightAnimByTrigger(const CubeAnimationTrigger& animTrigger,
                                                const AnimLayerEnum& layer,
                                                const ObjectID& objectID,
                                                bool shouldPickNextAnim)
{
  bool success = false;
  const auto iter = _triggerToHandleMap.find(animTrigger);
  if(iter != _triggerToHandleMap.end()){
    success = StopLightAnimInternal(iter->second, layer, objectID);
    _triggerToHandleMap.erase(iter);
  }
  return success;
}

bool CubeLightComponent::StopLightAnimInternal(const AnimationHandle& animationHandle,
                                               const AnimLayerEnum& layer,
                                               const ObjectID& objectID,
                                               bool shouldPickNextAnim)
{  
  auto helper = [this](const AnimationHandle& animationHandle,
                       const ObjectID& objectID,
                       const AnimLayerEnum& layer,
                       bool& foundAnimWithTrigger,
                       std::string& debugName) {
    auto& iter = _objectInfo[objectID];
    for(auto& anim : iter.animationsOnLayer[layer])
    {
      if(anim.animationHandle == animationHandle)
      {
        anim.stopNow = true;
        foundAnimWithTrigger = true;
        debugName = anim.debugName;
      }
    }
  };

  bool foundAnimWithTrigger = false;
  std::string debugName;
  if(objectID.IsUnknown())
  {
    for(auto& iter : _objectInfo)
    {
      helper(animationHandle, iter.first, layer, foundAnimWithTrigger, debugName);
    }
  }
  else
  {
    const auto& iter = _objectInfo.find(objectID);
    if(iter != _objectInfo.end())
    {
      helper(animationHandle, objectID, layer, foundAnimWithTrigger, debugName);
    }
  }

  if(foundAnimWithTrigger){
    PRINT_CH_INFO("CubeLightComponent", "CubeLightComponent.StopLightAnim",
                  "Stopping %s on object %d on layer %s.",
                  debugName.c_str(),
                  objectID.GetValue(),
                  LayerToString(layer));
  }

  
  // Manually update so the anims are immediately stopped
  UpdateInternal(shouldPickNextAnim);

  std::stringstream ss;
  for( const auto& objectInfoPair : _objectInfo ){
    if( objectID.IsSet() && objectID != objectInfoPair.first ) {
      continue;
    }
    
    ss << "object=" << objectInfoPair.first.GetValue() << " layer=" << LayerToString(layer)
       << " currLayer=" << LayerToString(objectInfoPair.second.curLayer) << " [";
    
    for(auto& anim : objectInfoPair.second.animationsOnLayer[layer]) {
      ss << anim.debugName << "@" << anim.timeCurPatternEnds;
      if( anim.stopNow ) {
        ss << ":STOP_NOW";
      }
      ss << " ";
    }
    ss << "]";
  }

  PRINT_CH_DEBUG("CubeLightComponent", "CubeLightComponent.StopLignAnim.Result",
                 "%s anim '%s'. Current state: %s",
                 foundAnimWithTrigger ? "found" : "did not find",
                 debugName.c_str(),
                 ss.str().c_str());
    
  return foundAnimWithTrigger;
}

void CubeLightComponent::ApplyAnimModifier(const CubeLightAnimation::Animation& anim,
                                           const CubeLightAnimation::ObjectLights& modifier,
                                           CubeLightAnimation::Animation& modifiedAnim)
{
  modifiedAnim.clear();
  modifiedAnim.insert(modifiedAnim.begin(), anim.begin(), anim.end());

  // For every pattern in the animation apply the modifier
  for(auto& pattern : modifiedAnim)
  {
    for(int i = 0; i < CubeLightAnimation::kNumCubeLEDs; ++i)
    {
      pattern.lights.onColors[i] |= modifier.onColors[i];
      pattern.lights.offColors[i] |= modifier.offColors[i];
      pattern.lights.onPeriod_ms[i] += modifier.onPeriod_ms[i];
      pattern.lights.offPeriod_ms[i] += modifier.offPeriod_ms[i];
      pattern.lights.transitionOnPeriod_ms[i] += modifier.transitionOnPeriod_ms[i];
      pattern.lights.transitionOffPeriod_ms[i] += modifier.transitionOffPeriod_ms[i];
      pattern.lights.offset[i] += modifier.offset[i];
      pattern.lights.rotate |= modifier.rotate;
    }
    pattern.lights.makeRelative = modifier.makeRelative;
    pattern.lights.relativePoint = modifier.relativePoint;
  }
}

bool CubeLightComponent::BlendAnimWithCurLights(const ObjectID& objectID,
                                                const CubeLightAnimation::Animation& anim,
                                                CubeLightAnimation::Animation& blendedAnim)
{
  auto* activeObject = _robot->GetBlockWorld().GetConnectedBlockByID(objectID);
  if(activeObject == nullptr)
  {
    PRINT_CH_INFO("CubeLightComponent", "CubeLightComponent.BlendAnimWithCurLights.NullActiveObject",
                  "No active object with id %u found in block world unable to blend anims",
                  objectID.GetValue());
    return false;
  }
  
  blendedAnim.clear();
  blendedAnim.insert(blendedAnim.begin(), anim.begin(), anim.end());
  
  bool res = false;
  
  // For every pattern in the anim replace the on/offColors that are completely zero
  // (completely zero meaning alpha is 0 as well as r,g,b are 0, normally alpha should be 1)
  // with whatever the corresponding color that is currently being displayed on the object
  for(auto& pattern : blendedAnim)
  {
    for(int i = 0; i < CubeLightAnimation::kNumCubeLEDs; ++i)
    {
      const Block::LEDstate& ledState = activeObject->GetLEDState(i);
      if(pattern.lights.onColors[i] == 0)
      {
        pattern.lights.onColors[i] = ledState.onColor.AsRGBA();
        res = true;
      }
      if(pattern.lights.offColors[i] == 0)
      {
        pattern.lights.offColors[i] = ledState.offColor.AsRGBA();
        res = true;
      }
    }
  }

  return res;
}

bool CubeLightComponent::StopAndPlayLightAnim(const ObjectID& objectID,
                                              const CubeAnimationTrigger& animTriggerToStop,
                                              const CubeAnimationTrigger& animTriggerToPlay,
                                              AnimCompletedCallback callback,
                                              bool hasModifier,
                                              const CubeLightAnimation::ObjectLights& modifier)
{
  DEV_ASSERT(!_objectInfo[objectID].animationsOnLayer[AnimLayerEnum::Engine].empty(),
             "CubeLightComponent.StopAndPlayLightAnim.NoAnimsCurrentlyOnEngineLayer");
  // Stop the anim and prevent the update call in StopLightAnim from picking a next default anim
  // This will prevent the lights from briefly flickering between the calls to stop and play
  StopLightAnimByTrigger(animTriggerToStop, AnimLayerEnum::Engine, objectID, false);
  bool playDuringBackgroundConnection = true;
  return PlayLightAnimByTrigger(objectID,
                                animTriggerToPlay,
                                callback,
                                playDuringBackgroundConnection,
                                hasModifier,
                                modifier);
}

bool CubeLightComponent::StopAndPlayLightAnim(const ObjectID& objectID,
                                              AnimationHandle& handle,
                                              CubeLightAnimation::Animation& animToPlay, 
                                              const std::string& debugName,
                                              AnimCompletedCallback callback,
                                              bool hasModifier,
                                              const CubeLightAnimation::ObjectLights& modifier)
{
  DEV_ASSERT(!_objectInfo[objectID].animationsOnLayer[AnimLayerEnum::Engine].empty(),
             "CubeLightComponent.StopAndPlayLightAnim.NoAnimsCurrentlyOnEngineLayer");
  // Stop the anim and prevent the update call in StopLightAnim from picking a next default anim
  // This will prevent the lights from briefly flickering between the calls to stop and play
  StopLightAnimInternal(handle, AnimLayerEnum::Engine, objectID, false);
  return PlayLightAnimInternal(objectID, animToPlay, AnimLayerEnum::Engine, callback,
                               hasModifier, modifier, 0, debugName, handle);

}

// =============Connction & Status Light Controls==================

bool CubeLightComponent::PlayConnectionLights(const ObjectID& objectID, AnimCompletedCallback callback)
{
  // Connect lights take precedence over other anims
  StopAllAnimsOnLayer(AnimLayerEnum::State);
  return PlayLightAnimByTrigger(objectID, CubeAnimationTrigger::WakeUp, AnimLayerEnum::State, callback);
}

bool CubeLightComponent::PlayDisconnectionLights(const ObjectID& objectID, AnimCompletedCallback callback)
{
  // Disconnect lights take precedence over other anims
  StopAllAnimsOnLayer(AnimLayerEnum::State);
  return PlayLightAnimByTrigger(objectID, CubeAnimationTrigger::ShutDown, AnimLayerEnum::State, callback);
}

bool CubeLightComponent::CancelDisconnectionLights(const ObjectID& objectID)
{
  return StopLightAnimByTrigger(CubeAnimationTrigger::ShutDown, AnimLayerEnum::State, objectID);
}

bool CubeLightComponent::PlayTapResponseLights(const ObjectID& objectID, AnimCompletedCallback callback)
{
  // Cube tap response lights should take precedence over other state anims
  StopAllAnimsOnLayer(AnimLayerEnum::State);
  return PlayLightAnimByTrigger(objectID, CubeAnimationTrigger::TapResponsePulse, AnimLayerEnum::State, callback);
}

bool CubeLightComponent::SetCubeBackgroundState(const bool connectedInBackground)
{
  bool stateChanged = (_cubeConnectedInBackground != connectedInBackground);
  _cubeConnectedInBackground = connectedInBackground;

  if(stateChanged && _cubeConnectedInBackground){
    StopAllAnimsOnLayer(AnimLayerEnum::State);
    StopAllAnimsOnLayer(AnimLayerEnum::Engine);
  }

  return stateChanged;
}

void CubeLightComponent::PickNextAnimForDefaultLayer(const ObjectID& objectID)
{
  if(DEBUG_TEST_ALL_ANIM_TRIGGERS)
  {
    CycleThroughAnimTriggers(objectID);
    return;
  }

  // If status anims are disabled for "background" cube connections, play a blank anim
  if(_cubeConnectedInBackground){
    // TODO:(str) remove the "cube sleep" infrastructure?
    PlayLightAnimByTrigger(objectID, CubeAnimationTrigger::SleepNoFade, AnimLayerEnum::State);
    return;
  }
  
  CubeAnimationTrigger animTrigger = CubeAnimationTrigger::Connected;
  
  const ObservableObject* object = _robot->GetBlockWorld().GetLocatedObjectByID(objectID);
  if(object != nullptr)
  {
    if(object->IsPoseStateKnown())
    {
      animTrigger = CubeAnimationTrigger::Visible;
    }
  }
  
  if(_robot->GetCarryingComponent().GetCarryingObjectID() == objectID)
  {
    animTrigger = CubeAnimationTrigger::Carrying;
  }
  
  PlayLightAnimByTrigger(objectID, animTrigger, AnimLayerEnum::State);
}

void CubeLightComponent::EnableGameLayerOnly(const ObjectID& objectID, bool enable)
{
  PRINT_CH_INFO("CubeLightComponent", "CubeLightComponent.EnableGameLayerOnly",
                "%s game layer only for %s",
                (enable ? "Enabling" : "Disabling"),
                (objectID.IsUnknown() ? "all objects" :
                 ("object " + std::to_string(objectID.GetValue())).c_str()));
  
  if(objectID.IsUnknown())
  {
    if( _onlyGameLayerEnabledForAll != enable ) {
      if(enable)
      {
        SetCubeLightAnimation(objectID, CubeLightAnimation::GetLightsOffObjectLights());
        for(auto& pair : _objectInfo)
        {
          pair.second.isOnlyGameLayerEnabled = true;
        }
        StopAllAnimsOnLayer(AnimLayerEnum::Engine);
        StopAllAnimsOnLayer(AnimLayerEnum::State);
      }
      else
      {
        StopAllAnimsOnLayer(AnimLayerEnum::User);
      
        for(auto& pair : _objectInfo)
        {
          pair.second.isOnlyGameLayerEnabled = false;
          if( pair.second.animationsOnLayer[AnimLayerEnum::Engine].empty() ) {
            pair.second.curLayer = AnimLayerEnum::State;
          }
          else {
            pair.second.curLayer = AnimLayerEnum::Engine;
          }
          PickNextAnimForDefaultLayer(pair.first);
        }
      }
      _onlyGameLayerEnabledForAll = enable;
    }
  }
  else
  {
    auto iter = _objectInfo.find(objectID);
    if(iter != _objectInfo.end())
    {
      if( enable != iter->second.isOnlyGameLayerEnabled ) {
        if(enable)
        {
          SetCubeLightAnimation(objectID, CubeLightAnimation::GetLightsOffObjectLights());
          StopAllAnimsOnLayer(AnimLayerEnum::Engine, objectID);
          StopAllAnimsOnLayer(AnimLayerEnum::State, objectID);
          iter->second.isOnlyGameLayerEnabled = true;
        }
        else
        {
          StopAllAnimsOnLayer(AnimLayerEnum::User, objectID);
          StopAllAnimsOnLayer(AnimLayerEnum::State, objectID);
          if( iter->second.animationsOnLayer[AnimLayerEnum::Engine].empty() ) {
            iter->second.curLayer = AnimLayerEnum::State;
          }
          else {
            iter->second.curLayer = AnimLayerEnum::Engine;
          }
          iter->second.isOnlyGameLayerEnabled = false;
          PickNextAnimForDefaultLayer(objectID);
        }
      }
    }
  }
}

const char* CubeLightComponent::LayerToString(const AnimLayerEnum& layer) const
{
  switch(layer)
  {
    case AnimLayerEnum::State:
      return "State";
    case AnimLayerEnum::Engine:
      return "Engine";
    case AnimLayerEnum::User:
      return "User/Game";
    case AnimLayerEnum::Count:
      return "Invalid";
  }
}

// =============Message handlers for cube lights==================

template<>
void CubeLightComponent::HandleMessage(const ExternalInterface::ObjectConnectionState& msg)
{
  if(msg.connected && IsValidLightCube(msg.object_type, false))
  {
    // Add the objectID to the _objectInfo map
    ObjectInfo info = {};
    info.isOnlyGameLayerEnabled = _onlyGameLayerEnabledForAll;
    _objectInfo.emplace(msg.objectID, info);
  }
}

template<>
void CubeLightComponent::HandleMessage(const ExternalInterface::PlayCubeAnim& msg)
{
  ObjectID objectID = msg.objectID;
  if(objectID < 0) {
    objectID = _robot->GetBlockWorld().GetSelectedObject();
  }
  
  PlayLightAnimByTrigger(objectID, msg.trigger, AnimLayerEnum::User);
}

template<>
void CubeLightComponent::HandleMessage(const ExternalInterface::StopCubeAnim& msg)
{
  ObjectID objectID = msg.objectID;
  if(objectID < 0) {
    objectID = _robot->GetBlockWorld().GetSelectedObject();
  }
  
  StopLightAnimByTrigger(msg.trigger, AnimLayerEnum::User, objectID);
}

void CubeLightComponent::CycleThroughAnimTriggers(const ObjectID& objectID)
{
  // Each time the function is called play the next CubeAnimationTrigger
  static int t = 0;
  PlayLightAnimByTrigger(objectID, static_cast<CubeAnimationTrigger>(t));
  t++;
  if(t >= static_cast<int>(CubeAnimationTrigger::Count))
  {
    t = 0;
  }
}

template<>
void CubeLightComponent::HandleMessage(const ExternalInterface::SetAllActiveObjectLEDs& msg)
{
  const CubeLightAnimation::ObjectLights lights {
    .onColors               = msg.onColor,
    .offColors              = msg.offColor,
    .onPeriod_ms            = msg.onPeriod_ms,
    .offPeriod_ms           = msg.offPeriod_ms,
    .transitionOnPeriod_ms  = msg.transitionOnPeriod_ms,
    .transitionOffPeriod_ms = msg.transitionOffPeriod_ms,
    .offset                 = msg.offset,
    .rotate                 = msg.rotate,
    .makeRelative           = msg.makeRelative,
    .relativePoint          = {msg.relativeToX,msg.relativeToY}
  };
  
  SetCubeLightAnimation(msg.objectID, lights);
}

template<>
void CubeLightComponent::HandleMessage(const ExternalInterface::SetActiveObjectLEDs& msg)
{
  SetCubeLightAnimation(msg.objectID,
                  msg.whichLEDs,
                  msg.onColor,
                  msg.offColor,
                  msg.onPeriod_ms,
                  msg.offPeriod_ms,
                  msg.transitionOnPeriod_ms,
                  msg.transitionOffPeriod_ms,
                  msg.turnOffUnspecifiedLEDs,
                  msg.makeRelative,
                  {msg.relativeToX, msg.relativeToY},
                  msg.rotate);
}

template<>
void CubeLightComponent::HandleMessage(const ExternalInterface::RobotDelocalized& msg)
{
  _robotDelocalized = true;
  
  // Set light states back to connected since Cozmo has lost track of all objects
  // (except for carried objects, because we still know where those are)
  for(auto& pair: _objectInfo)
  {
    const bool isCarryingCube = _robot->GetCarryingComponent().IsCarryingObject(pair.first);
    if(!isCarryingCube)
    {
      PlayLightAnimByTrigger(pair.first, CubeAnimationTrigger::Connected, AnimLayerEnum::State);
    }
  }
}

template<>
void CubeLightComponent::HandleMessage(const ExternalInterface::EnableLightStates& msg)
{
  // When enable == true in the EnableLightStates message all layers should be enabled
  // so we don't want to only enable the game layer hence the negation of msg.enable
  // This is so the EnableLightStates message did not need to change
  EnableGameLayerOnly(msg.objectID, !msg.enable);
}

Result CubeLightComponent::SetCubeLightAnimation(const ObjectID& objectID, const CubeLightAnimation::ObjectLights& values)
{
  Block* activeObject = nullptr;
  if ( values.makeRelative == MakeRelativeMode::RELATIVE_LED_MODE_OFF )
  {
    // note this could be a checked_cast
    activeObject = dynamic_cast<Block*>( _robot->GetBlockWorld().GetConnectedBlockByID(objectID) );
  }
  else
  {
    // note this could be a checked_cast
    activeObject = dynamic_cast<Block*>( _robot->GetBlockWorld().GetLocatedObjectByID(objectID) );
  }
  
  if(activeObject == nullptr)
  {
    PRINT_CH_INFO("CubeLightController",
                  "CubeLightController.SetCubeLightAnimation.NullActiveObject",
                  "Null active object pointer");
    return RESULT_FAIL_INVALID_OBJECT;
  }
  
  activeObject->SetLEDs(values.onColors,
                        values.offColors,
                        values.onPeriod_ms,
                        values.offPeriod_ms,
                        values.transitionOnPeriod_ms,
                        values.transitionOffPeriod_ms,
                        values.offset);
  
  auto* activeCube = dynamic_cast<Block*>(activeObject);
  if(activeCube == nullptr)
  {
    PRINT_CH_INFO("CubeLightController",
                  "CubeLightController.SetCubeLightAnimation.NullActiveCube",
                  "Null active cube pointer");
    return RESULT_FAIL_INVALID_OBJECT;
  }
  
  // NOTE: if make relative mode is "off", this call doesn't do anything:
  activeCube->MakeStateRelativeToXY(values.relativePoint, values.makeRelative);
  
  return SetLights(activeObject, values.rotate);
}

Result CubeLightComponent::SetCubeLightAnimation(const ObjectID& objectID,
                                            const WhichCubeLEDs whichLEDs,
                                            const u32 onColor,
                                            const u32 offColor,
                                            const u32 onPeriod_ms,
                                            const u32 offPeriod_ms,
                                            const u32 transitionOnPeriod_ms,
                                            const u32 transitionOffPeriod_ms,
                                            const bool turnOffUnspecifiedLEDs,
                                            const MakeRelativeMode makeRelative,
                                            const Point2f& relativeToPoint,
                                            const bool rotate)
{
  Block* activeCube = nullptr;
  if ( makeRelative == MakeRelativeMode::RELATIVE_LED_MODE_OFF )
  {
    // note this could be a checked_cast
    activeCube = dynamic_cast<Block*>( _robot->GetBlockWorld().GetConnectedBlockByID(objectID) );
  }
  else
  {
    // note this could be a checked_cast
    activeCube = dynamic_cast<Block*>( _robot->GetBlockWorld().GetLocatedObjectByID(objectID) );
  }
  
  // trying to do relative mode in an object that is not located in the current origin, will fail, since its
  // pose doesn't mean anything for relative purposes
  if(activeCube == nullptr)
  {
    PRINT_CH_INFO("CubeLightComponent",
                  "CubeLightComponent.SetCubeLightAnimation.NullActiveCube",
                  "Null active cube pointer (was it null or not a cube?)");
    return RESULT_FAIL_INVALID_OBJECT;
  }
  
  WhichCubeLEDs rotatedWhichLEDs = whichLEDs;
  // NOTE: if make relative mode is "off", this call doesn't do anything:
  rotatedWhichLEDs = activeCube->MakeWhichLEDsRelativeToXY(whichLEDs, relativeToPoint, makeRelative);
  
  activeCube->SetLEDs(rotatedWhichLEDs, onColor, offColor, onPeriod_ms, offPeriod_ms,
                      transitionOnPeriod_ms, transitionOffPeriod_ms, 0,
                      turnOffUnspecifiedLEDs);
  
  return SetLights(activeCube, rotate);
}

  
bool CubeLightComponent::CanEngineSetLightsOnCube(const ObjectID& objectID)
{
  const auto& info = _objectInfo.find(objectID);
  if(info != _objectInfo.end()){
    return !info->second.isOnlyGameLayerEnabled;
  }
  return false;
}


Result CubeLightComponent::SetLights(const Block* object, const bool rotate)
{
  CubeLights cubeLights;
  cubeLights.rotate = rotate;
  for(int i = 0; i < CubeLightAnimation::kNumCubeLEDs; ++i)
  {
    // Apply white balancing and encode colors
    const Block::LEDstate ledState = object->GetLEDState(i);
    cubeLights.lights[i].onColor  = WhiteBalanceColor(ledState.onColor);
    cubeLights.lights[i].offColor = WhiteBalanceColor(ledState.offColor);
    cubeLights.lights[i].onFrames  = MS_TO_LED_FRAMES(ledState.onPeriod_ms);
    cubeLights.lights[i].offFrames = MS_TO_LED_FRAMES(ledState.offPeriod_ms);
    cubeLights.lights[i].transitionOnFrames  = MS_TO_LED_FRAMES(ledState.transitionOnPeriod_ms);
    cubeLights.lights[i].transitionOffFrames = MS_TO_LED_FRAMES(ledState.transitionOffPeriod_ms);
    cubeLights.lights[i].offset = MS_TO_LED_FRAMES(ledState.offset);
    
    if(DEBUG_LIGHTS)
    {
      PRINT_CH_DEBUG("CubeLightComponent", "CubeLightComponent.SetLights",
                     "LED %u, onColor 0x%x (0x%x), offColor 0x%x (0x%x), onFrames 0x%x (%ums), "
                     "offFrames 0x%x (%ums), transOnFrames 0x%x (%ums), transOffFrames 0x%x (%ums), offset 0x%x (%ums)",
                     i, cubeLights.lights[i].onColor, ledState.onColor.AsRGBA(),
                     cubeLights.lights[i].offColor, ledState.offColor.AsRGBA(),
                     cubeLights.lights[i].onFrames, ledState.onPeriod_ms,
                     cubeLights.lights[i].offFrames, ledState.offPeriod_ms,
                     cubeLights.lights[i].transitionOnFrames, ledState.transitionOnPeriod_ms,
                     cubeLights.lights[i].transitionOffFrames, ledState.transitionOffPeriod_ms,
                     cubeLights.lights[i].offset, ledState.offset);
    }
  }
  
  if(DEBUG_LIGHTS)
  {
    PRINT_CH_DEBUG("CubeLightComponent", "CubeLightComponent.SetLights",
                   "Setting lights for object %d (activeID %d)",
                   object->GetID().GetValue(), object->GetActiveID());
  }

  bool result = false;
  const auto& activeId = object->GetActiveID();
  if (activeId != _robot->GetCubeCommsComponent().GetConnectedCubeActiveId()) {
    PRINT_NAMED_WARNING("CubeLightComponent.SetLights.ObjectNotConnected",
                        "Object with activeId %d is not connected",
                        activeId);
  } else {
    result = _robot->GetCubeCommsComponent().SendCubeLights(cubeLights);
  }

  return result ? RESULT_OK : RESULT_FAIL;
}

// TEMP (Kevin): WhiteBalancing is eventually to be done in body so just doing something simple here to get us by.
//               Basically if there is any red at all, then blue and green channels are scaled down to 60%.
ColorRGBA CubeLightComponent::WhiteBalanceColor(const ColorRGBA& origColor) const
{
  ColorRGBA color = origColor;
  if(color.GetR() > 0) {
    color.SetB( kWhiteBalanceScale * color.GetB());
    color.SetG( kWhiteBalanceScale * color.GetG());
  }
  return color;
}

void CubeLightComponent::HandleAppRequest(const AppToEngineEvent& event)
{
  using namespace external_interface;

  switch(event.GetData().GetTag()) {
    case GatewayWrapperTag::kSetCubeLightsRequest:
    {
      PRINT_NAMED_INFO("CubeLightComponent.HandleAppRequest.SetCubeLightsRequest",
                       "Received a request from gateway to set cube lights colors.");

      const auto& request = event.GetData().set_cube_lights_request();

      std::array<unsigned int, 4> onColor;
      std::copy(request.on_color().begin(), request.on_color().end(), onColor.begin());

      std::array<unsigned int, 4> offColor;
      std::copy(request.off_color().begin(), request.off_color().end(), offColor.begin());

      std::array<unsigned int, 4> onPeriodMs;
      std::copy(request.on_period_ms().begin(), request.on_period_ms().end(), onPeriodMs.begin());

      std::array<unsigned int, 4> offPeriodMs;
      std::copy(request.off_period_ms().begin(), request.off_period_ms().end(), offPeriodMs.begin());

      std::array<unsigned int, 4> transitionOnPeriodMs;
      std::copy(request.transition_on_period_ms().begin(), request.transition_on_period_ms().end(), transitionOnPeriodMs.begin());

      std::array<unsigned int, 4> transitionOffPeriodMs;
      std::copy(request.transition_off_period_ms().begin(), request.transition_off_period_ms().end(), transitionOffPeriodMs.begin());

      std::array<int, 4> offset;
      std::copy(request.offset().begin(), request.offset().end(), offset.begin());

      if( request.make_relative() == 0 )
      {
        PRINT_NAMED_WARNING("CubeLightComponent.HandleAppRequest.SetCubeLightsRequest",
                            "Got relative mode 0 (Unknown) from gateway, ignoring call");
      }
      else
      {
        const CubeLightAnimation::ObjectLights lights {
          .onColors               = onColor,
          .offColors              = offColor,
          .onPeriod_ms            = onPeriodMs,
          .offPeriod_ms           = offPeriodMs,
          .transitionOnPeriod_ms  = transitionOnPeriodMs,
          .transitionOffPeriod_ms = transitionOffPeriodMs,
          .offset                 = offset,
          .rotate                 = request.rotate(),
          .makeRelative           = (Anki::Vector::MakeRelativeMode)((int)request.make_relative()-1),
          .relativePoint          = {request.relative_to_x(),request.relative_to_y()}
        };

        const CubeLightAnimation::LightPattern pattern {
          .name                   = "SetCubeLightsRequest_GeneratedWrappedAnimation",
          .lights                 = lights,
          .duration_ms            = 0,
          .canBeOverridden        = true
        };

        CubeLightAnimation::Animation animation = { pattern };

        AnimationHandle handle;

        // TODO: Fully integrate these light calls with the sdk behavior VIC-4872
        PlayLightAnimInternal(request.object_id(), animation, AnimLayerEnum::Engine, nullptr, false, {}, 0, "GeneratedWrappedAnimation", handle);
      }
      break;
    }

    default:
      DEV_ASSERT(false, "CubeLightComponent.HandleAppRequest.UnhandledTag");
      break;
  }
}

}
}

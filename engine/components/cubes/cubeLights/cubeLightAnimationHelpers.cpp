/**
 * File: cubeLightAnimationHelpers.cpp
 *
 * Author: Kevin M. Karol
 * Created: 6/19/18
 *
 * Description: Defines the structs/classes related to cube lights in engine
 *
 * Copyright: Anki, Inc. 2018
 *
 **/


#include "engine/components/cubes/cubeLights/cubeLightAnimationHelpers.h"

#include "coretech/common/shared/math/point.h"
#include "coretech/common/engine/jsonTools.h"
#include "util/logging/logging.h"

#include <algorithm>

namespace Anki {
namespace Vector {
namespace CubeLightAnimation {


namespace{
// Pattern Keys
static const char* kOnColorsKey            = "onColors";
static const char* kOffColorsKey           = "offColors";
static const char* kOnPeriodKey            = "onPeriod_ms";
static const char* kOffPeriodKey           = "offPeriod_ms";
static const char* kTransitionOnPeriodKey  = "transitionOnPeriod_ms";
static const char* kTransitionOffPeriodKey = "transitionOffPeriod_ms";
static const char* kOffsetKey              = "offset";
static const char* kRotateKey              = "rotate";

// Sequence Keys
static const char* kDurationKey            = "duration_ms";
static const char* kPatternKey             = "pattern";
static const char* kCanBeOverriddenKey     = "canBeOverridden";
static const char* kPatternDebugNameKey    = "patternDebugName";
}

bool ParseCubeAnimationFromJson(const std::string& animName, const Json::Value& jsonRoot, CubeLightAnimation::Animation& outAnimation)
{
  if(!ANKI_VERIFY(jsonRoot.isArray(), 
                  "CubeLightAnimation.ParseCubeAnimationFromJson.BaseElementIsNotAnArray", 
                  "Cube animations are expected to be specified as an array of light patterns")){
    return false;
  }
  
  const s32 numPatterns = jsonRoot.size();
  
  for(s32 patternNum = 0; patternNum < numPatterns; ++patternNum){
    LightPattern pattern;
    const auto& lightPattern = jsonRoot[patternNum];
    
    if(!lightPattern.isMember(kPatternKey) ||
       !lightPattern.isMember(kDurationKey))
    {
      PRINT_NAMED_ERROR("CubeLightAnimationContainer.ParseJsonToAnim.InvalidJson",
                        "Missing member field in %s json file",
                        animName.c_str());
      return false;
    }
    
    const bool res = ParseJsonToPattern(lightPattern[kPatternKey], pattern);
    if(!res)
    {
      PRINT_NAMED_ERROR("CubeLightAnimationContainer.ParseJsonToAnim.InvalidPattern",
                        "Missing member field in one of the pattern definitions in %s json file",
                        animName.c_str());
      return false;
    }
    
    pattern.duration_ms = lightPattern[kDurationKey].asUInt();
    
    if(lightPattern.isMember(kCanBeOverriddenKey))
    {
      pattern.canBeOverridden = lightPattern[kCanBeOverriddenKey].asBool();
    }
    
    // Pattern name debug if defined, otherwise animation name and pattern idx
    if(lightPattern.isMember(kPatternDebugNameKey)){
      pattern.name = lightPattern[kPatternDebugNameKey].asString();
    }else{
      pattern.name = animName + std::to_string(patternNum);
    }
    
    outAnimation.push_back(std::move(pattern));
  }

  return true;
}


bool ParseJsonToPattern(const Json::Value& json, LightPattern& pattern)
{
  ObjectLights values;
  
  bool res = true;
  res &= JsonTools::GetColorValuesToArrayOptional(json, kOnColorsKey,  values.onColors);
  res &= JsonTools::GetColorValuesToArrayOptional(json, kOffColorsKey, values.offColors);
  res &= JsonTools::GetArrayOptional(json, kOnPeriodKey,               values.onPeriod_ms);
  res &= JsonTools::GetArrayOptional(json, kOffPeriodKey,              values.offPeriod_ms);
  res &= JsonTools::GetArrayOptional(json, kTransitionOnPeriodKey,     values.transitionOnPeriod_ms);
  res &= JsonTools::GetArrayOptional(json, kTransitionOffPeriodKey,    values.transitionOffPeriod_ms);
  res &= JsonTools::GetArrayOptional(json, kOffsetKey,                 values.offset);
  res &= json.isMember(kRotateKey);
  
  // If any of the member fields are missing from the pattern definition return false
  if(!res)
  {
    return false;
  }
  
  values.rotate            = json[kRotateKey].asBool();
  values.makeRelative      = MakeRelativeMode::RELATIVE_LED_MODE_OFF;
  values.relativePoint     = {0,0};
  
  pattern.lights = std::move(values);
  
  return true;
}


Json::Value LEDArrayToJson(const ObjectLEDArray& array)
{
  Json::Value outJson;
  int i = 0;
  for(u32 data: array){
    outJson[i] = data;
    i++;
  }

  return outJson;
}


Json::Value SignedArrayToJson(const std::array<s32, 4>& array)
{
  Json::Value outJson;
  int i = 0;
  for(u32 data: array){
    outJson[i] = data;
    i++;
  }

  return outJson;
}

Json::Value AnimationToJSON(const std::string& animName, const Animation& animation)
{
  int i = 0;
  Json::Value patternArray;
  for(const auto& pattern: animation){
    // Pattern metadata
    Json::Value patternAndMetadata;
    patternAndMetadata[kDurationKey] = pattern.duration_ms;
    patternAndMetadata[kPatternDebugNameKey] = pattern.name;
    // actual pattern
    {
      Json::Value patternJSON;
      {
        // On Color array
        Json::Value onColorArray;
        auto colorArrayIdx = 0;
        for(const auto& color: pattern.lights.onColors){
          ColorRGBA rgba(color);
          Json::Value colorArray;
          colorArray[0] = rgba.r();
          colorArray[1] = rgba.g();
          colorArray[2] = rgba.b();
          colorArray[3] = rgba.alpha();

          onColorArray[colorArrayIdx] = colorArray;
          colorArrayIdx++;
        }
        patternJSON[kOnColorsKey] = onColorArray;
      }
      {
        // Off Color array
        Json::Value offColorArray;
        auto colorArrayIdx = 0;
        for(const auto& color: pattern.lights.offColors){
          ColorRGBA rgba(color);
          Json::Value colorArray;
          colorArray[0] = rgba.r();
          colorArray[1] = rgba.g();
          colorArray[2] = rgba.b();
          colorArray[3] = rgba.alpha();

          offColorArray[colorArrayIdx] = colorArray;
          colorArrayIdx++;
        }
        patternJSON[kOffColorsKey] = offColorArray;
      }

      patternJSON[kOnPeriodKey] = LEDArrayToJson(pattern.lights.onPeriod_ms);
      patternJSON[kOffPeriodKey] = LEDArrayToJson(pattern.lights.offPeriod_ms);
      patternJSON[kTransitionOnPeriodKey] = LEDArrayToJson(pattern.lights.transitionOnPeriod_ms);
      patternJSON[kTransitionOffPeriodKey] = LEDArrayToJson(pattern.lights.transitionOffPeriod_ms);

      patternJSON[kOffsetKey] = SignedArrayToJson(pattern.lights.offset);
      patternJSON[kRotateKey] = pattern.lights.rotate;


      patternAndMetadata[kPatternKey] = patternJSON;
    }

    patternArray[i] = patternAndMetadata;
  }

  return patternArray;
}


void LightPattern::Print() const
{
  for(int i = 0; i < static_cast<int>(CubeConstants::NUM_CUBE_LEDS); i++)
  {
    PRINT_CH_DEBUG("CubeLightComponent", "LightPattern.Print",
                   "%s LED %u, onColor %u, offColor %u, onFrames %u, "
                   "offFrames %u, transOnFrames %u, transOffFrames %u, offset %u",
                   name.c_str(),
                   i, lights.onColors[i],
                   lights.offColors[i],
                   lights.onPeriod_ms[i],
                   lights.offPeriod_ms[i],
                   lights.transitionOnPeriod_ms[i],
                   lights.transitionOffPeriod_ms[i],
                   lights.offset[i]);
  }
}
  
void RotateLightPatternCounterClockwise(LightPattern& pattern, uint8_t positionsToRotate)
{
  if(positionsToRotate > kNumCubeLEDs){
    PRINT_NAMED_ERROR("CubeLightAnimationHelpers.RotateLightPatternClockwise.InvalidRotationNumber",
                      "Can only rotate LEDs %d positions, received request to rotate %d positions",
                      kNumCubeLEDs, positionsToRotate);
    return;
  }
  #define ROTATE_ARRAY(property) std::rotate(pattern.lights.property.begin(),\
                                             pattern.lights.property.begin() + (CubeLightAnimation::kNumCubeLEDs - positionsToRotate),\
                                             pattern.lights.property.end());

  ROTATE_ARRAY(onColors)
  ROTATE_ARRAY(offColors)
  ROTATE_ARRAY(onPeriod_ms)
  ROTATE_ARRAY(offPeriod_ms)
  ROTATE_ARRAY(transitionOnPeriod_ms)
  ROTATE_ARRAY(transitionOffPeriod_ms)
  ROTATE_ARRAY(offset)

  #undef ROTATE_ARRAY
}


void OverwriteLEDs(const LightPattern& source, LightPattern& toOverwrite, const std::set<uint8_t> ledsToOverwrite)
{
  for(const auto& led: ledsToOverwrite){
    toOverwrite.lights.onColors[led]               = source.lights.onColors[led];
    toOverwrite.lights.offColors[led]              = source.lights.offColors[led];
    toOverwrite.lights.onPeriod_ms[led]            = source.lights.onPeriod_ms[led];
    toOverwrite.lights.offPeriod_ms[led]           = source.lights.offPeriod_ms[led];
    toOverwrite.lights.transitionOnPeriod_ms[led]  = source.lights.transitionOnPeriod_ms[led];
    toOverwrite.lights.transitionOffPeriod_ms[led] = source.lights.transitionOffPeriod_ms[led];
    toOverwrite.lights.offset[led]                 = source.lights.offset[led];
  }
}

const ObjectLights& GetLightsOffObjectLights()
{
  static const CubeLightAnimation::ObjectLights kCubeLightsOff = {
    .onColors               = {{NamedColors::BLACK, NamedColors::BLACK, NamedColors::BLACK, NamedColors::BLACK}},
    .offColors              = {{NamedColors::BLACK, NamedColors::BLACK, NamedColors::BLACK, NamedColors::BLACK}},
    .onPeriod_ms            = {{0,0,0,0}},
    .offPeriod_ms           = {{0,0,0,0}},
    .transitionOnPeriod_ms  = {{0,0,0,0}},
    .transitionOffPeriod_ms = {{0,0,0,0}},
    .offset                 = {{0,0,0,0}},
    .rotate                 = false,
    .makeRelative           = MakeRelativeMode::RELATIVE_LED_MODE_OFF,
    .relativePoint          = {0,0}
  };
  return kCubeLightsOff;
}


const LightPattern& GetLightsOffPattern()
{
  static const LightPattern kLightsOffPattern = {
    .name = "LightsOffLights",
    .lights =  GetLightsOffObjectLights()
  };

  return kLightsOffPattern;
}

} // namespace CubeLightAnimation
} // namespace Vector
} // namespace Anki

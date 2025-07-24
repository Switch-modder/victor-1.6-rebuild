/**
 * File: behaviorDevImageCapture.cpp
 *
 * Author: Brad Neuman
 * Created: 2017-12-12
 *
 * Description: Dev behavior to use the touch sensor or backpack button to enable / disable image capture
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/behaviorDevImageCapture.h"

#include "clad/types/imageTypes.h"

#include "coretech/common/engine/utils/data/dataPlatform.h"
#include "coretech/common/engine/jsonTools.h"
#include "coretech/common/engine/utils/timer.h"

#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/behaviorExternalInterface.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/beiRobotInfo.h"
#include "engine/actions/basicActions.h"
#include "engine/actions/compoundActions.h"
#include "engine/audio/engineRobotAudioClient.h"
#include "engine/components/backpackLights/engineBackpackLightComponent.h"
#include "engine/components/movementComponent.h"
#include "engine/components/sensors/touchSensorComponent.h"
#include "engine/components/visionComponent.h"
#include "engine/cozmoContext.h"
#include "engine/vision/imageSaver.h"
#include "osState/osState.h"
#include "util/fileUtils/fileUtils.h"

#define LOG_CHANNEL "Behaviors"

namespace Anki {
namespace Vector {

namespace {

constexpr const float kLightBlinkPeriod_s = 0.5f;
constexpr const float kHoldTimeForStreaming_s = 1.0f;

static const BackpackLightAnimation::BackpackAnimation kLightsOn = {
  .onColors               = {{NamedColors::BLACK,NamedColors::RED,NamedColors::BLACK}},
  .offColors              = {{NamedColors::BLACK,NamedColors::RED,NamedColors::BLACK}},
  .onPeriod_ms            = {{0,0,0}},
  .offPeriod_ms           = {{0,0,0}},
  .transitionOnPeriod_ms  = {{0,0,0}},
  .transitionOffPeriod_ms = {{0,0,0}},
  .offset                 = {{0,0,0}}
};

static const BackpackLightAnimation::BackpackAnimation kLightsOff = {
  .onColors               = {{NamedColors::BLACK,NamedColors::BLACK,NamedColors::BLACK}},
  .offColors              = {{NamedColors::BLACK,NamedColors::BLACK,NamedColors::BLACK}},
  .onPeriod_ms            = {{0,0,0}},
  .offPeriod_ms           = {{0,0,0}},
  .transitionOnPeriod_ms  = {{0,0,0}},
  .transitionOffPeriod_ms = {{0,0,0}},
  .offset                 = {{0,0,0}}
};
  
const char* const kSavePathKey = "save_path";
const char* const kImageSaveQualityKey = "quality";
const char* const kImageScaleKey = "image_scale";
const char* const kUseCapacitiveTouchKey = "use_capacitive_touch";
const char* const kUseShutterSoundKey = "use_shutter_sound";
const char* const kAllowStreamingKey = "allow_streaming";
const char* const kSaveSensorDataKey = "save_sensor_data";
const char* const kClassNamesKey = "class_names";
const char* const kVisionModesKey = "vision_modes";
const char* const kUseSavePrefixKey = "use_save_prefix";
  
const char* const kMultiImageModeKey = "multi_image_mode";
const char* const kNumImagesPerCaptureKey = "num_images_per_capture";
const char* const kDistanceRangeKey = "distance_range_mm";
const char* const kHeadAngleRangeKey = "head_angle_range_deg";
const char* const kBodyAngleRangeKey = "body_angle_range_deg";
  
const char* const kFalsePositiveStr = "FalsePositives";
const char* const kFalseNegativeStr = "FalseNegatives";
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorDevImageCapture::InstanceConfig::InstanceConfig()
  : imageSaveQuality(-1)
  , imageSaveSize(Vision::ImageCacheSize::Half)
  , useSavePrefix(false)
  , useCapTouch(false)
  , saveSensorData(false)
  , useShutterSound(true)
  , allowStreaming(true)
  , numImagesPerCapture(1)
  , distanceRange_mm{0,0}
  , headAngleRange_rad{0,0}
  , bodyAngleRange_rad{0,0}
{
  
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorDevImageCapture::DynamicVariables::DynamicVariables()
{
  touchStartedTime_s = -1.0f; 

  blinkOn = false;
  timeToBlink = -1.0f;

  isStreaming = false;
  wasLiftUp = false;
  
  startingBodyAngle_rad = 0.f;
  startingHeadAngle_rad = 0.f;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
static inline void SetRangeHelper(const Json::Value& config, const char* const key,
                                  std::pair<f32,f32>& range, bool isAngle)
{
  if(config.isMember(key))
  {
    const Json::Value& rangeMember = config[key];
    if(rangeMember.isArray() && rangeMember.size()==2)
    {
      range.first  = rangeMember[0].asFloat();
      range.second = rangeMember[1].asFloat();
      if(isAngle)
      {
        range.first  = DEG_TO_RAD(range.first);
        range.second = DEG_TO_RAD(range.second);
      }
      return;
    }
  }
  
  LOG_ERROR("BehaviorDevImageCapture.SetRangeHelper.Failure", "Failed to set %s range", key);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorDevImageCapture::BehaviorDevImageCapture(const Json::Value& config)
  : ICozmoBehavior(config)
{
  _iConfig.imageSavePath = JsonTools::ParseString(config, kSavePathKey, "BehaviorDevImageCapture");
  _iConfig.imageSaveQuality = JsonTools::ParseInt8(config, kImageSaveQualityKey, "BehaviorDevImageCapture");
  JsonTools::GetValueOptional(config, kUseSavePrefixKey, _iConfig.useSavePrefix);
  _iConfig.useCapTouch = JsonTools::ParseBool(config, kUseCapacitiveTouchKey, "BehaviorDevImageCapture");
  _iConfig.useShutterSound = JsonTools::ParseBool(config, kUseShutterSoundKey, "BehaviorDevImageCapture");
  _iConfig.allowStreaming = JsonTools::ParseBool(config, kAllowStreamingKey, "BehaviorDevImageCapture");
  std::string scaleStr = JsonTools::ParseString(config, kImageScaleKey, "BehaviorDevImageCapture");
  _iConfig.imageSaveSize = Vision::ImageCache::StringToSize(scaleStr);
  
  if(config.isMember(kMultiImageModeKey))
  {
    const Json::Value& multiImageConfig = config[kMultiImageModeKey];
    _iConfig.numImagesPerCapture = JsonTools::ParseInt32(multiImageConfig, kNumImagesPerCaptureKey,
                                                         "BehaviorDevImageCapture");
    SetRangeHelper(multiImageConfig, kDistanceRangeKey,  _iConfig.distanceRange_mm,   false);
    SetRangeHelper(multiImageConfig, kHeadAngleRangeKey, _iConfig.headAngleRange_rad, true);
    SetRangeHelper(multiImageConfig, kBodyAngleRangeKey, _iConfig.bodyAngleRange_rad, true);
  }
  
  if(config.isMember(kSaveSensorDataKey)) {
    _iConfig.saveSensorData = config[kSaveSensorDataKey].asBool();
  }
  
  if(config.isMember(kClassNamesKey))
  {
    auto const& classNames = config[kClassNamesKey];
    if(classNames.isArray())
    {
      for(Json::ArrayIndex index=0; index < classNames.size(); ++index)
      {
        _iConfig.classNames.push_back(classNames[index].asString());
      }
    }
    else if(classNames.isString())
    {
      _iConfig.classNames.push_back(classNames.asString());
    }
    else 
    {
      LOG_WARNING("BehaviorDevImageCapture.Constructor.InvalidClassNames", "");
    }
  }
  _dVars.currentClassIter = _iConfig.classNames.begin();
  
  if(config.isMember(kVisionModesKey))
  {
    auto const & visionModes = config[kVisionModesKey];

    auto setVisionModeHelper = [this](const Json::Value& jsonVisionMode)
    {
      const std::string& visionModeStr = jsonVisionMode.asString();
      VisionMode visionMode;
      const bool success = VisionModeFromString(visionModeStr, visionMode);
      if(success)
      {
        _iConfig.visionModesBesidesSaving.push_back(visionMode);
      }
      else
      {
        LOG_WARNING("BehaviorDevImageCapture.Constructor.InvalidVisionMode", "%s", visionModeStr.c_str());
      }
    };
    
    if(visionModes.isArray())
    {
      std::for_each(visionModes.begin(), visionModes.end(), setVisionModeHelper);
    }
    else if(visionModes.isString())
    {
      setVisionModeHelper(visionModes);
    }
    else
    {
      LOG_WARNING("BehaviorDevImageCapture.Constructor.InvalidVisionModeEntry", "");
    }
  }

  // Grab the serial number to use in the filename:
  auto *osstate = OSState::getInstance();
  _iConfig.serialNumber = osstate->GetSerialNumberAsString();
  _iConfig.buildSha = osstate->GetBuildSha();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorDevImageCapture::~BehaviorDevImageCapture()
{
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorDevImageCapture::InitBehavior()
{
  if(_iConfig.useSavePrefix)
  {
    // If we have a special saving prefix string in persistent data, use that.
    // This is how we work around the fact that times may not be right and could
    // thus be non-unique if the robot eventually talks to a time server somewhere
    // and updates itself after having already saved images.
    const auto context = GetBEI().GetRobotInfo().GetContext();
    const std::string prefixFileName = context->GetDataPlatform()->GetPersistentPath("devImageCapturePrefix.txt");
    if(Util::FileUtils::FileExists(prefixFileName))
    {
      _iConfig.imageSavePrefix = Util::FileUtils::ReadFile(prefixFileName);
      _iConfig.imageSavePrefix += "_";
      LOG_INFO("BehaviorDevImageCapture.Constructor.UsingPrefix.", "%s", _iConfig.imageSavePrefix.c_str());
    }
  }
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorDevImageCapture::GetBehaviorOperationModifiers(BehaviorOperationModifiers& modifiers) const
{
  modifiers.wantsToBeActivatedWhenOffTreads = true;
  modifiers.wantsToBeActivatedWhenOnCharger = true;
  modifiers.behaviorAlwaysDelegates = false;
  modifiers.visionModesForActiveScope->insert({VisionMode::MirrorMode,   EVisionUpdateFrequency::High});
  modifiers.visionModesForActiveScope->insert({VisionMode::SaveImages, EVisionUpdateFrequency::High});
  for(auto const& mode : _iConfig.visionModesBesidesSaving)
  {
    // TODO: support non-"Standard" frequency specification?
    modifiers.visionModesForActiveScope->insert({mode, EVisionUpdateFrequency::Standard});
  }
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorDevImageCapture::GetBehaviorJsonKeys(std::set<const char*>& expectedKeys) const
{
  const char* list[] = {
    kSavePathKey,
    kImageSaveQualityKey,
    kImageScaleKey,
    kUseCapacitiveTouchKey,
    kSaveSensorDataKey,
    kUseShutterSoundKey,
    kAllowStreamingKey,
    kClassNamesKey,
    kVisionModesKey,
    kMultiImageModeKey,
    kUseSavePrefixKey,
  };
  expectedKeys.insert( std::begin(list), std::end(list) );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorDevImageCapture::OnBehaviorActivated()
{
  _dVars.touchStartedTime_s = -1.0f;
  _dVars.isStreaming = false;
  _dVars.timeToBlink = -1.0f;
  _dVars.blinkOn = false;
  _dVars.imagesSaved = 0; // NOTE: Only increments in SingleShot* modes
  
  auto& robotInfo = GetBEI().GetRobotInfo();
  // wait for the lift to relax 
  robotInfo.GetMoveComponent().EnableLiftPower(false);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorDevImageCapture::OnBehaviorDeactivated()
{
  auto& robotInfo = GetBEI().GetRobotInfo();
  // wait for the lift to relax 
  robotInfo.GetMoveComponent().EnableLiftPower(true);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorDevImageCapture::SwitchToNextClass()
{
  if(!_iConfig.classNames.empty())
  {
    ++_dVars.currentClassIter; 
    if(_dVars.currentClassIter == _iConfig.classNames.end())
    {
      _dVars.currentClassIter = _iConfig.classNames.begin();
    }
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
std::string BehaviorDevImageCapture::GetSavePath() const
{
  const std::string& cachePath = GetBEI().GetRobotInfo().GetContext()->GetDataPlatform()->GetCachePath("camera");
  
  if(_dVars.currentClassIter == _iConfig.classNames.end())
  {
    return Util::FileUtils::FullFilePath({cachePath, "images", _iConfig.imageSavePath});
  }
  
  return Util::FileUtils::FullFilePath({cachePath, "images", _iConfig.imageSavePath, *_dVars.currentClassIter});
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorDevImageCapture::BehaviorUpdate()
{
  if(!IsActivated())
  {
    return;
  }
  
  const bool doingMultiImageCapture = (_iConfig.numImagesPerCapture > 1);
  const bool haveStartedCapture     = (_dVars.imagesSaved >= 1);
  const bool haveCompletedCapture   = (_dVars.imagesSaved >= _iConfig.numImagesPerCapture);
  if(!_dVars.isStreaming && doingMultiImageCapture && haveStartedCapture && !haveCompletedCapture)
  {
    // Ignore additional button presses / touches while we're in the process of taking
    // multiple images at different poses
    return;
  }
  
  const float currTime_s = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
  auto& visionComponent = GetBEI().GetComponentWrapper(BEIComponentID::Vision).GetComponent<VisionComponent>();

  // update light blinking if needed
  if( _dVars.timeToBlink > 0.0f ) {
    if( currTime_s >= _dVars.timeToBlink ) {
      BlinkLight();
    }
  }

  // Switch classes when lift is lowered
  const bool isLiftUp = (GetBEI().GetRobotInfo().GetLiftHeight() > LIFT_HEIGHT_HIGHDOCK);
  if(_dVars.wasLiftUp && !isLiftUp)
  {
    SwitchToNextClass();
  }
  _dVars.wasLiftUp = isLiftUp;

  if(_dVars.currentClassIter != _iConfig.classNames.end())
  {
    using namespace Util;
    
    const size_t numFiles = FileUtils::FilesInDirectory(GetSavePath(), false,
                                                        ImageSaver::GetExtension(_iConfig.imageSaveQuality)).size();

    const std::string str(*_dVars.currentClassIter + ":" + std::to_string(numFiles));
    visionComponent.SetMirrorModeDisplayString(str, NamedColors::YELLOW);
  }

  const bool wasTouched = (_dVars.touchStartedTime_s >= 0.0f);

  const bool isTouched = (_iConfig.useCapTouch ? 
                          GetBEI().GetTouchSensorComponent().GetIsPressed() :
                          GetBEI().GetRobotInfo().IsPowerButtonPressed());

  if( wasTouched && !isTouched ) {
    // just "released", see if it's been long enough to count as a "hold" for toggling streaming
    ImageSendMode sendMode = ImageSendMode::Off;
    if( _iConfig.allowStreaming && (currTime_s >= _dVars.touchStartedTime_s + kHoldTimeForStreaming_s) ) {
      LOG_DEBUG("BehaviorDevImageCapture.touch.longPress", "long press release");
        
      // toggle streaming
      _dVars.isStreaming = !_dVars.isStreaming;

      sendMode = (_dVars.isStreaming ? ImageSendMode::Stream : ImageSendMode::Off);
      
      if( _dVars.isStreaming ) {
        BlinkLight();
      }
    }
    else {
      LOG_DEBUG("BehaviorDevImageCapture.touch.shortPress", "short press release");
      // take single photo
      _dVars.imagesSaved = 0;
      sendMode = (_iConfig.saveSensorData ? ImageSendMode::SingleShotWithSensorData : ImageSendMode::SingleShot);
      BlinkLight();
    }
  
    SaveImages(sendMode);
  }
  else if( !wasTouched && isTouched ) {
    LOG_DEBUG("BehaviorDevImageCapture.touch.newTouch", "new press");
    _dVars.touchStartedTime_s = currTime_s;

    if( _dVars.isStreaming ) {
      // we were streaming but now should stop because there was a new touch
      ImageSaverParams params(GetSavePath(),
                              ImageSendMode::Off,
                              _iConfig.imageSaveQuality,
                              "", // No basename: rely on auto-numbering
                              _iConfig.imageSaveSize);
      visionComponent.SetSaveImageParameters(params);

      _dVars.isStreaming = false;
    }
  }

  if( !isTouched ) {
    _dVars.touchStartedTime_s = -1.0f;
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorDevImageCapture::BlinkLight()
{
  _dVars.blinkOn = !_dVars.blinkOn;

  GetBEI().GetBackpackLightComponent().SetBackpackAnimation( _dVars.blinkOn ? kLightsOn : kLightsOff );

  // always blink if we are streaming, and set up another blink for single photo if we need to turn off the
  // light
  if( _dVars.blinkOn || _dVars.isStreaming ) {
    const float currTime_s = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
    // need to do another blink after this one
    _dVars.timeToBlink = currTime_s + kLightBlinkPeriod_s;
  }
  else {
    _dVars.timeToBlink = -1.0f;
  }
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorDevImageCapture::MoveToNewPose()
{
  LOG_DEBUG("BehaviorDevImageCapture.MoveToNewPose", "Moving to pose %d of %d",
            _dVars.imagesSaved+1, _iConfig.numImagesPerCapture);
  
  // For each valid range (where the max > min), compute a random angle/distance and create
  // a compound action to perform the movements. Note that head and body turns can be done
  // in parallel. We then do the body movement _afterward_. This could be streamlined a bit
  // to handle special cases that we have, for example just a head and drive action (but no
  // body turn action), since we could also run those in parallel. But I'm not gonna
  // overoptimize this... In general, we'll probably use all three movements.
  CompoundActionParallel* turnAction = new CompoundActionParallel();
  
  if(Util::IsFltLT(_iConfig.headAngleRange_rad.first, _iConfig.headAngleRange_rad.second))
  {
    const f32 headAngleDelta_rad = GetRNG().RandDblInRange(_iConfig.headAngleRange_rad.first,
                                                           _iConfig.headAngleRange_rad.second);
    const f32 headAngle_rad = Util::Clamp((_dVars.startingHeadAngle_rad + headAngleDelta_rad).ToFloat(),
                                          MIN_HEAD_ANGLE, MAX_HEAD_ANGLE);
    turnAction->AddAction(new MoveHeadToAngleAction(headAngle_rad));
    LOG_DEBUG("BehaviorDevImageCapture.MoveToNewPose.HeadAngle", "%.1fdeg", RAD_TO_DEG(headAngle_rad));
  }
  
  if(Util::IsFltLT(_iConfig.bodyAngleRange_rad.first, _iConfig.bodyAngleRange_rad.second))
  {
    const f32 bodyAngleDelta_rad = GetRNG().RandDblInRange(_iConfig.bodyAngleRange_rad.first,
                                                           _iConfig.bodyAngleRange_rad.second);
    const f32 bodyAngle_rad = (_dVars.startingBodyAngle_rad + bodyAngleDelta_rad).ToFloat();
    const bool isAbsolute = true; // using absolute because we want to be relative to starting angle, not current
    turnAction->AddAction(new TurnInPlaceAction(bodyAngle_rad, isAbsolute));
    LOG_DEBUG("BehaviorDevImageCapture.MoveToNewPose.BodyAngle",
              "Start:%.1fdeg Current:%.1fdeg Delta:%.1fdeg NewAngle:%.1fdeg",
              _dVars.startingBodyAngle_rad.getDegrees(),
              GetBEI().GetRobotInfo().GetPose().GetRotationAngle<'Z'>().getDegrees(),
              RAD_TO_DEG(bodyAngleDelta_rad), RAD_TO_DEG(bodyAngle_rad));
  }
  
  CompoundActionSequential* action = new CompoundActionSequential();
    
  if(turnAction->GetNumActions() > 0)
  {
    action->AddAction(turnAction);
  }
  
  if(Util::IsFltLT(_iConfig.distanceRange_mm.first, _iConfig.distanceRange_mm.second))
  {
    const f32 dist_mm = GetRNG().RandDblInRange(_iConfig.distanceRange_mm.first, _iConfig.distanceRange_mm.second);
    const bool kPlayAnim = false;
    action->AddAction(new DriveStraightAction(dist_mm, DEFAULT_PATH_MOTION_PROFILE.speed_mmps, kPlayAnim));
    LOG_DEBUG("BehaviorDevImageCapture.MoveToNewPose.Distance", "%.1fmm", dist_mm);
  }
  
  DelegateIfInControl(action, [this]() {
    const ImageSendMode sendMode = (_iConfig.saveSensorData ?
                                    ImageSendMode::SingleShotWithSensorData :
                                    ImageSendMode::SingleShot);
    SaveImages(sendMode);
  });
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorDevImageCapture::SaveImages(const ImageSendMode sendMode)
{
  // To help avoid duplicate images names when combining images from multiple robots on multiple runs of
  // this behavior, use a basename built from the robot's serial number and the seconds since epoch.
  // If multiple images are being captured, a counter is appended as well.
  // Note that for streaming, a frame number will also be appended by the ImageSaver because all saved
  // images will share the same basename (since they comes from the same button press).
  using namespace std::chrono;
  const auto time_sec = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
  std::string basename = (_iConfig.imageSavePrefix
                          + _iConfig.serialNumber + "_"
                          + _iConfig.buildSha + "_"
                          + std::to_string(time_sec));
  if(_iConfig.numImagesPerCapture > 1)
  {
    basename += "_" + std::to_string(_dVars.imagesSaved);
  }
  
  // Tell VisionComponent to save an image
  ImageSaverParams params(GetSavePath(),
                          sendMode,
                          _iConfig.imageSaveQuality,
                          basename,
                          _iConfig.imageSaveSize);
  
  // Special case: if class name is FalsePositive or FalseNegative, set up save conditions to
  //  only save image at the right time
  if(!_iConfig.visionModesBesidesSaving.empty() && _dVars.currentClassIter != _iConfig.classNames.end())
  {
    const std::string currentClass(*_dVars.currentClassIter);
    if(kFalsePositiveStr == currentClass)
    {
      for(const auto visionMode : _iConfig.visionModesBesidesSaving)
      {
        params.saveConditions[visionMode] = ImageSaverParams::SaveConditionType::OnDetection;
      }
    }
    else if(kFalseNegativeStr == currentClass)
    {
      for(const auto visionMode : _iConfig.visionModesBesidesSaving)
      {
        params.saveConditions[visionMode] = ImageSaverParams::SaveConditionType::NoDetection;
      }
    }
  }
  
  auto& visionComponent = GetBEI().GetComponentWrapper(BEIComponentID::Vision).GetComponent<VisionComponent>();
  visionComponent.SetSaveImageParameters(params);
  
  if(!_dVars.isStreaming)
  {
    WaitForImagesAction* waitAction = new WaitForImagesAction(1, VisionMode::SaveImages);
    
    DelegateIfInControl(waitAction, [this]() {
      _dVars.imagesSaved++;
      
      if(!_dVars.isStreaming && _iConfig.useShutterSound)
      {
        // Shutter sound once save completes
        using GE = AudioMetaData::GameEvent::GenericEvent;
        using GO = AudioMetaData::GameObjectType;
        GetBEI().GetRobotAudioClient().PostEvent(GE::Play__Robot_Vic_Sfx__Camera_Flash,
                                                 GO::Behavior);
      }
      
      if(_dVars.imagesSaved == 1)
      {
        // After the first image is taken, store starting angles for each button press so we can move to
        // new positions relative to that
        _dVars.startingBodyAngle_rad = GetBEI().GetRobotInfo().GetPose().GetRotationAngle<'Z'>();
        _dVars.startingHeadAngle_rad = GetBEI().GetRobotInfo().GetHeadAngle();
      }
      if(_dVars.imagesSaved < _iConfig.numImagesPerCapture)
      {
        MoveToNewPose();
      }
    });
  }
}


} // namespace Vector
} // namespace Anki

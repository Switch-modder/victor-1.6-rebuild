/**
 * File: playpenConfig.h
 *
 * Author: Al Chaussee
 * Created: 08/10/17
 *
 * Description: Configuration file for playpen
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/playpen/playpenConfig.h"
#include "util/console/consoleInterface.h"

namespace Anki {
namespace Util {

// ConsoleVar specialization for CustomObjectMarker
template<>
ConsoleVar<Vector::CustomObjectMarker>::ConsoleVar(Vector::CustomObjectMarker& value, const char* id, const char* category, bool unregisterInDestructor )
: IConsoleVariable( id, category, unregisterInDestructor )
, _value( value )
, _minValue( Vector::CustomObjectMarker(0) )
, _maxValue( Vector::CustomObjectMarker::Count )
, _defaultValue( value )
{
}

template<> bool ConsoleVar<Vector::CustomObjectMarker>::ParseText(const char* text)
{
  _value = Vector::CustomObjectMarkerFromString(text);
  return true;
}

template<> std::string ConsoleVar<Vector::CustomObjectMarker>::ToString() const
{
  return Vector::EnumToString(_value);
}

template<> std::string ConsoleVar<Vector::CustomObjectMarker>::GetDefaultAsString() const
{
  return Vector::EnumToString(_defaultValue);
}

template<> int64_t ConsoleVar<Vector::CustomObjectMarker>::GetMinAsInt64() const
{
  return numeric_cast_clamped<int64_t>((int)_minValue);
}

template<> int64_t ConsoleVar<Vector::CustomObjectMarker>::GetMaxAsInt64() const
{
  return numeric_cast_clamped<int64_t>((int)_maxValue);
}

template<> uint64_t ConsoleVar<Vector::CustomObjectMarker>::GetMinAsUInt64() const
{
  return numeric_cast_clamped<uint64_t>((int)_minValue);
}

template<> uint64_t ConsoleVar<Vector::CustomObjectMarker>::GetMaxAsUInt64() const
{
  return numeric_cast_clamped<uint64_t>((int)_maxValue);
}

template<> void ConsoleVar<Vector::CustomObjectMarker>::ToggleValue() { _value = (Vector::CustomObjectMarker)(!(((bool)(_value)))); }
  
}

namespace Vector {
namespace PlaypenConfig {

WRAP_EXTERN_CONSOLE_VAR(bool,  kDisconnectAtEnd,              "Playpen");
WRAP_EXTERN_CONSOLE_VAR(bool,  kWriteToStorage,               "Playpen");
WRAP_EXTERN_CONSOLE_VAR(bool,  kIgnoreFailures,               "Playpen");
WRAP_EXTERN_CONSOLE_VAR(float, kDefaultTimeout_ms,            "Playpen");
WRAP_EXTERN_CONSOLE_VAR(float, kTimeToDisplayResultOnFace_ms, "Playpen");
WRAP_EXTERN_CONSOLE_VAR(u32,   kTouchDurationToStart_ms,      "Playpen");
WRAP_EXTERN_CONSOLE_VAR(bool,  kSkipActiveObjectCheck,        "Playpen");
WRAP_EXTERN_CONSOLE_VAR(bool,  kUseTouchToStart,              "Playpen");
WRAP_EXTERN_CONSOLE_VAR(bool,  kUseButtonToStart,             "Playpen");
WRAP_EXTERN_CONSOLE_VAR(u32,   kDurationOfTouchToRecord_ms,   "Playpen");

WRAP_EXTERN_CONSOLE_VAR(bool, kCheckFirmwareVersion,  "Playpen");
WRAP_EXTERN_CONSOLE_VAR(f32,  kMinBatteryVoltage,     "Playpen");
WRAP_EXTERN_CONSOLE_VAR(u32,  kMinFirmwareVersion,    "Playpen");
WRAP_EXTERN_CONSOLE_VAR(int,  kMinHardwareVersion,    "Playpen");
WRAP_EXTERN_CONSOLE_VAR(u32,  kMfgIDTimeout_ms,       "Playpen");
WRAP_EXTERN_CONSOLE_VAR(u16,  kMinExpectedTouchValue, "Playpen");
WRAP_EXTERN_CONSOLE_VAR(u16,  kMaxExpectedTouchValue, "Playpen");

WRAP_EXTERN_CONSOLE_VAR(u32, kMotorCalibrationTimeout_ms, "Playpen");

WRAP_EXTERN_CONSOLE_VAR(f32, kHeadAngleForDriftCheck,     "Playpen");
WRAP_EXTERN_CONSOLE_VAR(f32, kHeadAngleToPlaySound,       "Playpen");
WRAP_EXTERN_CONSOLE_VAR(f32, kSoundVolume,                "Playpen");
WRAP_EXTERN_CONSOLE_VAR(u32, kDurationOfAudioToRecord_ms, "Playpen");
WRAP_EXTERN_CONSOLE_VAR(u32, kFFTExpectedFreq_hz,         "Playpen");
WRAP_EXTERN_CONSOLE_VAR(u32, kFFTFreqTolerance_hz,        "Playpen");
WRAP_EXTERN_CONSOLE_VAR(u32, kIMUDriftDetectPeriod_ms,    "Playpen");
WRAP_EXTERN_CONSOLE_VAR(f32, kIMUDriftAngleThreshDeg,     "Playpen");

WRAP_EXTERN_CONSOLE_VAR(u32, kNumDistanceSensorReadingsToRecord,             "Playpen");
WRAP_EXTERN_CONSOLE_VAR(f32, kVisualDistanceToDistanceSensorObjectThresh_mm, "Playpen");
WRAP_EXTERN_CONSOLE_VAR(f32, kDistanceSensorReadingThresh_mm,                "Playpen");
WRAP_EXTERN_CONSOLE_VAR(f32, kDistanceSensorBiasAdjustment_mm,               "Playpen");

WRAP_EXTERN_CONSOLE_VAR(u16, kExposure_ms,                               "Playpen");
WRAP_EXTERN_CONSOLE_VAR(f32, kGain,                                      "Playpen");
WRAP_EXTERN_CONSOLE_VAR(u32, kFocalLengthTolerance,                      "Playpen");
WRAP_EXTERN_CONSOLE_VAR(u32, kCenterTolerance,                           "Playpen");
WRAP_EXTERN_CONSOLE_VAR(f32, kRadialDistortionTolerance,                 "Playpen");
WRAP_EXTERN_CONSOLE_VAR(f32, kTangentialDistortionTolerance,             "Playpen");
WRAP_EXTERN_CONSOLE_VAR(f32, kHeadAngleToSeeTarget_rad,                  "Playpen");
WRAP_EXTERN_CONSOLE_VAR(u32, kTimeoutWaitingForTarget_ms,                "Playpen");
WRAP_EXTERN_CONSOLE_VAR(u32, kTimeoutForCalibration_ms,         "Playpen");
WRAP_EXTERN_CONSOLE_VAR(f32, kCalibMarkerSize_mm,                        "Playpen");
WRAP_EXTERN_CONSOLE_VAR(u32, kPlaypenCalibTarget,                        "Playpen");
WRAP_EXTERN_CONSOLE_VAR(f32, kCalibMarkerCubeSize_mm,                    "Playpen");
WRAP_EXTERN_CONSOLE_VAR(CustomObjectMarker, kMarkerToTriggerCalibration, "Playpen");

WRAP_EXTERN_CONSOLE_VAR(f32, kDistanceToTriggerFrontCliffs_mm, "Playpen");
WRAP_EXTERN_CONSOLE_VAR(f32, kDistanceToTriggerBackCliffs_mm,  "Playpen");
WRAP_EXTERN_CONSOLE_VAR(f32, kDistanceToDriveOverCliff_mm,     "Playpen");
WRAP_EXTERN_CONSOLE_VAR(f32, kCliffSpeed_mmps,                 "Playpen");
WRAP_EXTERN_CONSOLE_VAR(f32, kTimeToWaitForCliffEvent_ms,      "Playpen");
WRAP_EXTERN_CONSOLE_VAR(u16, kCliffSensorThreshold,            "Playpen");

WRAP_EXTERN_CONSOLE_VAR(f32, kExpectedCubePoseX_mm,                "Playpen");
WRAP_EXTERN_CONSOLE_VAR(f32, kExpectedCubePoseY_mm,                "Playpen");
WRAP_EXTERN_CONSOLE_VAR(f32, kExpectedCubePoseDistThresh_mm,       "Playpen");
WRAP_EXTERN_CONSOLE_VAR(f32, kExpectedCubePoseHeightThresh_mm,     "Playpen");
WRAP_EXTERN_CONSOLE_VAR(f32, kExpectedCubePoseAngleThresh_rad,     "Playpen");
WRAP_EXTERN_CONSOLE_VAR(f32, kMaxRobotAngleChangeDuringBackup_rad, "Playpen");

}
}
}

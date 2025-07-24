/**
 * File: emotion
 *
 * Author: Mark Wesley
 * Created: 10/14/15
 *
 * Description: Tracks a single emotion (which together make up the mood for a Cozmo Robot)
 *
 * Copyright: Anki, Inc. 2015
 *
 **/


#include "engine/moodSystem/emotion.h"

#include "engine/moodSystem/moodDecayEvaluator.h"
#include "util/console/consoleInterface.h"
#include "util/graphEvaluator/graphEvaluator2d.h"
#include "util/logging/logging.h"
#include "util/math/math.h"
#include <assert.h>

namespace Anki {
namespace Vector {

  
const float kEmotionValueMin     = -1.0f;
const float kEmotionValueDefault =  0.0f;
const float kEmotionValueMax     =  1.0f;
  
#if REMOTE_CONSOLE_ENABLED
static const char* kEmotionSectionName = "Mood.Emotion";
#endif // REMOTE_CONSOLE_ENABLED
CONSOLE_VAR(uint32_t, kMaxEmotionHistorySamples, kEmotionSectionName,  128);

Emotion::Emotion()
  : _history(kMaxEmotionHistorySamples)
  , _value(kEmotionValueDefault)
  , _minValue(kEmotionValueMin)
  , _maxValue(kEmotionValueMax)
  , _timeDecaying(0.0f)
{
  _history.push_back( HistorySample(_value, 0.0f) );
}

  
void Emotion::Reset()
{
  _history.Reset(kMaxEmotionHistorySamples);
  _value = kEmotionValueDefault;
  _timeDecaying = 0.0f;
  _history.push_back( HistorySample(_value, 0.0f) );
}

  
float Emotion::GetHistoryValueTicksAgo(uint32_t numTicksBackwards) const
{
  const uint32_t numEntries = Util::numeric_cast<uint32_t>(_history.size());
  if ((numEntries > 0) && (numTicksBackwards > 0))
  {
    ++numTicksBackwards; // so that this method name is accurate, skip the last entry in _history, which is the current _value
    const uint32_t sampleIndex = (numEntries > numTicksBackwards) ? (numEntries - numTicksBackwards) : 0;
    const HistorySample& oldSample = _history[sampleIndex];
    return oldSample._value;
  }
  
  // use current value
  return _value;
}


float Emotion::GetHistoryValueSecondsAgo(float secondsBackwards) const
{
  assert(secondsBackwards >= 0.0f);
  
  const uint32_t numEntries = Util::numeric_cast<uint32_t>(_history.size());
  if ((numEntries > 0) && (secondsBackwards > 0.0f))
  {
    float timeRewound = 0.0f; // ideally this would be "time since last sample added" but we don't know that, so assume 0 (it must be < 1 tick)
    
    for (uint32_t i = numEntries; i > 0; --i)
    {
      const HistorySample& oldSample = _history[i-1];
      if (FLT_GE(timeRewound, secondsBackwards))
      {
        // interp? or nearest? or furthest?
        return oldSample._value;
      }
      
      // Note: This sample says how long it was since the previous one, so we only increment here (not before the test above)
      timeRewound += oldSample._timeDelta;
    }
    
    // No samples that far back - use the oldest one we have
    return _history[0]._value;
  }
  
  // use current value
  return _value;
}
  
  
void Emotion::Update(const MoodDecayEvaulator& evaluator, float timeDelta_s, float& velocity, float& accel)
{
  _value = evaluator.EvaluateDecay(_value, _timeDecaying, timeDelta_s, velocity, accel);

  _timeDecaying += timeDelta_s;
  
  _history.push_back( HistorySample(_value, timeDelta_s) );
  
  // clamp rate and accel based on emotion value range
  if( (_value <= _minValue) && (velocity < 0.0f) ) {
    velocity = 0.0f;
    accel = Anki::Util::Max(0.0f, accel);
  } else if( (_value >= _maxValue) && (velocity > 0.0f) ) {
    velocity = 0.0f;
    accel = Anki::Util::Min(0.0f, accel);
  }
}


void Emotion::Add(float penalizedDeltaValue)
{
  const float oldValue = _value;
  const float newValue = Anki::Util::Clamp(_value + penalizedDeltaValue, _minValue, _maxValue);
  _value = newValue;
  
  const bool isSufficentChangeToResetDecay = (Util::Abs(penalizedDeltaValue) > 0.05f); // penalty can prevent reset (if it scales value too far), but clamping at min/max cannot prevent it
  const bool isChangeInOppositeDirectionToDecay = ((oldValue >= 0.0f) == (penalizedDeltaValue >= 0.0f)); // only true if penalizedValue is driving value away from zero
  
  if (isSufficentChangeToResetDecay && isChangeInOppositeDirectionToDecay)
  {
    // Reset the decay (e.g. if +ve value was dropping back to 0 but we've just awarded a +ve score)
    _timeDecaying = 0.0f;
  }
  else if ((oldValue >= 0.0f) != (_value >= 0.0f)) // has crossed to/from 0
  {
    // Reset the decay we've crossed into opposite (+ve to -ve or vice versa)
    _timeDecaying = 0.0f;
  }
}
  
  
void Emotion::SetValue(float newValue)
{
  _value = newValue;
  _timeDecaying = 0.0f;
}

void Emotion::SetEmotionValueRange(float min, float max)
{
  ANKI_VERIFY(min <= max,
              "Emotion.SetEmotionValueRange.Invalid",
              "Invalid range %f - %f",
              min,
              max);
  _minValue = min;
  _maxValue = max;
}


} // namespace Vector
} // namespace Anki


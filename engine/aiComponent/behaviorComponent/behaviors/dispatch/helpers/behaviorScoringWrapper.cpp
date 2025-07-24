/**
 * File: BehaviorScoringWrapper.cpp
 *
 * Author: Brad Neuman
 * Created: 2017-10-31
 *
 * Description: Simple helper to track cooldowns
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#include "engine/aiComponent/behaviorComponent/behaviors/dispatch/helpers/behaviorScoringWrapper.h"

#include "clad/externalInterface/messageEngineToGame.h"

#include "coretech/common/engine/jsonTools.h"
#include "coretech/common/engine/utils/timer.h"

#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/beiRobotInfo.h"
#include "engine/externalInterface/externalInterface.h"
#include "json/json.h"
#include "util/random/randomGenerator.h"

namespace Anki {
namespace Vector {

namespace {
static const char* kEmotionScorersKey            = "emotionScorers";
static const char* kFlatScoreKey                 = "flatScore";
static const char* kRepetitionPenaltyKey         = "repetitionPenalty";
static const char* kActivatedPenaltyKey          = "activatedPenalty";
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorScoringWrapper::BehaviorScoringWrapper(const Json::Value& config)
{
  ReadFromJson(config);  
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorScoringWrapper::~BehaviorScoringWrapper()
{

}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorScoringWrapper::Init(BehaviorExternalInterface& bei)
{
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorScoringWrapper::BehaviorWillBeActivated()
{
  _timeActivated = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
  _isActivated = true;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorScoringWrapper::BehaviorDeactivated()
{
  _lastTimeDeactivated = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
  _isActivated = false;  
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
float BehaviorScoringWrapper::EvaluateScore(BehaviorExternalInterface& bei) const
{
  float score = _flatScore;
  if (!_moodScorer.IsEmpty()  &&
      bei.HasMoodManager()) {
    auto& moodManager = bei.GetMoodManager();
    score = _moodScorer.EvaluateEmotionScore(moodManager);
  }

  // use activated penalty while activated, repetition penalty otherwise
  if (_enableActivatedPenalty && _isActivated)
  {
    const float activatedPenalty = EvaluateActivatedPenalty();
    score *= activatedPenalty;
  }


  if (_enableRepetitionPenalty && !_isActivated)
  {
    const float repetitionPenalty = EvaluateRepetitionPenalty();
    score *= repetitionPenalty;
  }

  return score;
}



// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
float BehaviorScoringWrapper::EvaluateRepetitionPenalty() const
{
  if (_lastTimeDeactivated > 0.0f)
  {
    const float currentTime_sec = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
    const float timeSinceRun = currentTime_sec - _lastTimeDeactivated;
    const float repetitionPenalty = _repetitionPenalty.EvaluateY(timeSinceRun);
    return repetitionPenalty;
  }
  
  return 1.0f;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
float BehaviorScoringWrapper::EvaluateActivatedPenalty() const
{
  if (_timeActivated > 0.0f)
  {
    const float currentTime_sec = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
    const float timeSinceStarted = currentTime_sec - _timeActivated;
    const float activatedPenalty = _activatedPenalty.EvaluateY(timeSinceStarted);
    return activatedPenalty;
  }
  
  return 1.0f;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorScoringWrapper::ReadFromJson(const Json::Value& config)
{
  
  // - - - - - - - - - -
  // Mood scorer
  // - - - - - - - - - -
  _moodScorer.ClearEmotionScorers();
  
  const Json::Value& emotionScorersJson = config[kEmotionScorersKey];
  if (!emotionScorersJson.isNull())
  {
    _moodScorer.ReadFromJson(emotionScorersJson);
  }
  
  // - - - - - - - - - -
  // Flat score
  // - - - - - - - - - -
  
  const Json::Value& flatScoreJson = config[kFlatScoreKey];
  if (!flatScoreJson.isNull()) {
    _flatScore = flatScoreJson.asFloat();
  }
  
  // make sure we only set one scorer (flat or mood)
  DEV_ASSERT( flatScoreJson.isNull() || _moodScorer.IsEmpty(), "ICozmoBehavior.ReadFromJson.MultipleScorers" );
  
  // - - - - - - - - - -
  // Repetition penalty
  // - - - - - - - - - -
  
  _repetitionPenalty.Clear();
  
  const Json::Value& repetitionPenaltyJson = config[kRepetitionPenaltyKey];
  if (!repetitionPenaltyJson.isNull())
  {
    if (!_repetitionPenalty.ReadFromJson(repetitionPenaltyJson))
    {
      PRINT_NAMED_WARNING("IScoredBehavior.BadRepetitionPenalty",
                          "Behavior: %s failed to read",
                          kRepetitionPenaltyKey);
    }
  }
  
  
  // Ensure there is a valid graph
  if (_repetitionPenalty.GetNumNodes() == 0)
  {
    _repetitionPenalty.AddNode(0.0f, 1.0f); // no penalty for any value
  }
  
  
  // - - - - - - - - - -
  // Activated penalty
  // - - - - - - - - - -
  
  _activatedPenalty.Clear();
  
  const Json::Value& activatedPenaltyJson = config[kActivatedPenaltyKey];
  if (!activatedPenaltyJson.isNull())
  {
    if (!_activatedPenalty.ReadFromJson(activatedPenaltyJson))
    {
      PRINT_NAMED_WARNING("ICozmoBehavior.BadactivatedPenalty",
                          "Behavior: %s failed to read",
                          kActivatedPenaltyKey);
    }
  }
  
  // Ensure there is a valid graph
  if (_activatedPenalty.GetNumNodes() == 0)
  {
    _activatedPenalty.AddNode(0.0f, 1.0f); // no penalty for any value
  }
  return true;
}



} // namespace Vector
} // namespace Anki

/**
* File: conditionEmotion.h
*
* Author:  Kevin M. Karol
* Created: 11/1/17
*
* Description: Condition to compare against the value of an emotion
*
* Copyright: Anki, Inc. 2017
*
**/

#ifndef __Cozmo_Basestation_BehaviorSystem_WantsToRunStrategies_ConditionEmotion_H__
#define __Cozmo_Basestation_BehaviorSystem_WantsToRunStrategies_ConditionEmotion_H__

#include "engine/aiComponent/beiConditions/iBEICondition.h"

namespace Anki {
namespace Vector {
  
enum class EmotionType : uint8_t;

class ConditionEmotion : public IBEICondition
{
public:
  explicit ConditionEmotion(const Json::Value& config);

protected:
  virtual bool AreConditionsMetInternal(BehaviorExternalInterface& behaviorExternalInterface) const override;

private:
  void LoadJson(const Json::Value& config);
  
  bool _useRange;
  float _maxScore;
  float _minScore;
  float _exactScore;
  EmotionType _emotion;
};


} // namespace Vector
} // namespace Anki

#endif // __Cozmo_Basestation_BehaviorSystem_WantsToRunStrategies_ConditionEmotion_H__

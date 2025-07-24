/**
 * File: conditionBatteryLevel
 *
 * Author: Matt Michini
 * Created: 2018-03-07
 *
 * Description: Checks if the battery level matches the one supplied in config
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#ifndef __Engine_AiComponent_BeiConditions_Conditions_ConditionBatteryLevel_H__
#define __Engine_AiComponent_BeiConditions_Conditions_ConditionBatteryLevel_H__

#include "engine/aiComponent/beiConditions/iBEICondition.h"

#include "clad/types/batteryTypes.h"

namespace Anki {
namespace Vector {

class ConditionBatteryLevel : public IBEICondition
{
public:
  explicit ConditionBatteryLevel(const Json::Value& config);

  virtual bool AreConditionsMetInternal(BehaviorExternalInterface& bei) const override;

private:
  BatteryLevel _targetBatteryLevel;
  
};

}
}


#endif

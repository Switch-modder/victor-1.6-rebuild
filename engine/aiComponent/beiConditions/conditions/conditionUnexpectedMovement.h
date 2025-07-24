/**
 * File: conditionUnexpectedMovement
 *
 * Author: Matt Michini
 * Created: 4/10/18
 *
 * Description: Condition which is true when the robot detects unexpected movement (e.g.
 *              driving into an object and not following the expected path)
 *
 * Copyright: Anki, Inc. 2018
 *
 **/


#ifndef __Engine_BehaviorSystem_ConditionUnexpectedMovement_H__
#define __Engine_BehaviorSystem_ConditionUnexpectedMovement_H__

#include "engine/aiComponent/beiConditions/iBEICondition.h"

namespace Anki {
namespace Vector {

class BEIConditionMessageHelper;

class ConditionUnexpectedMovement : public IBEICondition
{
public:
  // constructor
  explicit ConditionUnexpectedMovement(const Json::Value& config);
  ~ConditionUnexpectedMovement() = default;
  
protected:
  virtual bool AreConditionsMetInternal(BehaviorExternalInterface& behaviorExternalInterface) const override;

};
  
} // namespace
} // namespace

#endif // endif __Engine_BehaviorSystem_ConditionUnexpectedMovement_H__

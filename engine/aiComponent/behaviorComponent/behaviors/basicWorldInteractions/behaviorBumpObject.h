/**
 * File: behaviorBumpObject.h
 *
 * Author: ross
 * Created: 2018-07-02
 *
 * Description: a playful bump of whatever is in front of the robot
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#ifndef __Engine_AiComponent_BehaviorComponent_Behaviors_BehaviorBumpObject__
#define __Engine_AiComponent_BehaviorComponent_Behaviors_BehaviorBumpObject__
#pragma once

#include "engine/aiComponent/behaviorComponent/behaviors/iCozmoBehavior.h"

namespace Anki {
namespace Vector {

class BehaviorBumpObject : public ICozmoBehavior
{
public: 
  virtual ~BehaviorBumpObject() = default;

protected:

  // Enforce creation through BehaviorFactory
  friend class BehaviorFactory;
  explicit BehaviorBumpObject(const Json::Value& config);  

  virtual void GetBehaviorOperationModifiers(BehaviorOperationModifiers& modifiers) const override;
  virtual void GetBehaviorJsonKeys(std::set<const char*>& expectedKeys) const override;
  
  virtual void InitBehavior() override;
  virtual void GetAllDelegates(std::set<IBehavior*>& delegates) const override;
  virtual bool WantsToBeActivatedBehavior() const override;
  virtual void OnBehaviorActivated() override;
  virtual void OnBehaviorDeactivated() override;
  virtual void BehaviorUpdate() override;

private:
  
  void DoFirstBump();
  void DoSecondBumpIfDesired();
  void DoReferenceHumanIfDesired();
  
  bool WouldBumpPushSomethingOffCliff( float driveDist_mm ) const;
  
  enum class State : uint8_t {
    Invalid=0,
    FirstBump,
    SecondBump,
    ReferenceHumanAfterBump
  };

  struct InstanceConfig {
    InstanceConfig();
    uint16_t minDist_mm;
    uint16_t maxDist_mm;
    float pEvil;
    float pBumpWhenEvil;
    float pBumpWhenNotEvil;
    ICozmoBehaviorPtr referenceHumanBehavior;
  };

  struct DynamicVariables {
    DynamicVariables();
    bool unexpectedMovement;
    State state;
    int bumpedAgain; // for DAS only: -1 for no, 0 for non evil, 1 for evil
  };

  InstanceConfig _iConfig;
  DynamicVariables _dVars;
  
};

} // namespace Vector
} // namespace Anki

#endif // __Engine_AiComponent_BehaviorComponent_Behaviors_BehaviorBumpObject__

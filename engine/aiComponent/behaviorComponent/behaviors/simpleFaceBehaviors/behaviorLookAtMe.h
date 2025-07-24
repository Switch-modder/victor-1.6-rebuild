/**
 * File: behaviorLookAtMe.h
 *
 * Author: ross
 * Created: 2018-06-22
 *
 * Description: Behavior for tracking a face
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#ifndef __Engine_AiComponent_BehaviorComponent_Behaviors_BehaviorLookAtMe__
#define __Engine_AiComponent_BehaviorComponent_Behaviors_BehaviorLookAtMe__
#pragma once

#include "engine/aiComponent/behaviorComponent/behaviors/simpleFaceBehaviors/iSimpleFaceBehavior.h"

namespace Anki {
namespace Vector {
  
enum class AnimationTrigger : int32_t;

class BehaviorLookAtMe : public ISimpleFaceBehavior
{
public: 
  virtual ~BehaviorLookAtMe() = default;

protected:

  // Enforce creation through BehaviorFactory
  friend class BehaviorFactory;
  explicit BehaviorLookAtMe(const Json::Value& config);  

  virtual void GetBehaviorOperationModifiers(BehaviorOperationModifiers& modifiers) const override;
  virtual void GetBehaviorJsonKeys(std::set<const char*>& expectedKeys) const override;
  
  virtual bool WantsToBeActivatedBehavior() const override;
  virtual void OnBehaviorActivated() override;
  virtual void OnBehaviorDeactivated() override;
  virtual void BehaviorUpdate() override;

private:

  struct InstanceConfig {
    InstanceConfig();
    float panTolerance_deg; // ignored if negative
    
    AnimationTrigger animGetIn;
    AnimationTrigger animLoop;
    AnimationTrigger animGetOut;
    unsigned int numLoops;
    bool playEmergencyGetOut;
  };

  struct DynamicVariables {
    DynamicVariables();
    bool startedGetOut;
    std::weak_ptr<IActionRunner> trackFaceAction;
    std::weak_ptr<IActionRunner> loopingAnimAction;
    std::weak_ptr<IActionRunner> getInAnimAction;
  };

  InstanceConfig _iConfig;
  DynamicVariables _dVars;
  
};

} // namespace Vector
} // namespace Anki

#endif // __Engine_AiComponent_BehaviorComponent_Behaviors_BehaviorLookAtMe__

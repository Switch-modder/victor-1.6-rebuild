/**
 * File: behaviorRollBlock.h
 *
 * Author: Brad Neuman
 * Created: 2016-04-05
 *
 * Description: This behavior rolls blocks when it see's them not upright
 *
 * Copyright: Anki, Inc. 2016
 *
 **/

#ifndef __Cozmo_Basestation_Behaviors_BehaviorRollBlock_H__
#define __Cozmo_Basestation_Behaviors_BehaviorRollBlock_H__

#include "coretech/common/engine/objectIDs.h"
#include "engine/aiComponent/behaviorComponent/behaviors/iCozmoBehavior.h"
#include "engine/aiComponent/objectInteractionInfoCache.h"

namespace Anki {
namespace Vector {

class BlockWorldFilter;
class ObservableObject;

class BehaviorRollBlock : public ICozmoBehavior
{
public:
  void SetTargetID(const ObjectID& targetID){
    _dVars.targetID = targetID;
    _dVars.idSetExternally = true;
  }
  
protected:
  // Enforce creation through BehaviorFactory
  friend class BehaviorFactory;
  BehaviorRollBlock(const Json::Value& config);

  virtual void GetBehaviorOperationModifiers(BehaviorOperationModifiers& modifiers) const override {
    modifiers.wantsToBeActivatedWhenCarryingObject = true;
  }
  virtual void GetBehaviorJsonKeys(std::set<const char*>& expectedKeys) const override;

  virtual void OnBehaviorActivated() override;
  virtual void BehaviorUpdate() override;

  virtual bool WantsToBeActivatedBehavior() const override;
    
private:
  enum class State {
    RollingBlock,
    CelebratingRoll
  };

  struct InstanceConfig{
    InstanceConfig();
    bool isBlockRotationImportant;
    int  rollRetryCount;
  };

  struct DynamicVariables{
    ObjectID targetID;
    bool     didAttemptDock        = false;
    AxisName upAxisOnBehaviorStart = AxisName::X_POS;
    State    behaviorState         = State::RollingBlock;
    int      rollRetryCount        = 0;
    bool     idSetExternally       = false;
  };

  InstanceConfig _iConfig;
  DynamicVariables _dVars;

  void TransitionToPerformingAction();
  void TransitionToRollSuccess();
  
  void CalculateTargetID(ObjectID& targetID) const;
  
  void UpdateTargetsUpAxis();
  
  void SetState_internal(State state, const std::string& stateName);

};

}
}

#endif

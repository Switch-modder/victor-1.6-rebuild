/**
 * File: BehaviorConnectToCube.h
 *
 * Author: Sam Russell
 * Created: 2018-06-29
 *
 * Description: "Gate" behavior that should preceed any behavior that requiresCubeConnection in the BehaviorStack. This is enforced in ICozmoBehavior::WantsToBeActivated at runtime, and in Behavior System Unit Tests.
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#ifndef __Engine_AiComponent_BehaviorComponent_Behaviors_BehaviorConnectToCube__
#define __Engine_AiComponent_BehaviorComponent_Behaviors_BehaviorConnectToCube__
#pragma once

#include "engine/aiComponent/behaviorComponent/behaviors/iCozmoBehavior.h"

namespace Anki {
namespace Vector {

class BehaviorConnectToCube : public ICozmoBehavior
{
public: 
  virtual ~BehaviorConnectToCube();

protected:

  // Enforce creation through BehaviorFactory
  friend class BehaviorFactory;
  explicit BehaviorConnectToCube(const Json::Value& config);  

  virtual void GetBehaviorOperationModifiers(BehaviorOperationModifiers& modifiers) const override {
    // Subscribe in OnActivated, unsubscribe in OnDeactivated
    modifiers.cubeConnectionRequirements = BehaviorOperationModifiers::CubeConnectionRequirements::OptionalActive;

    // Connect in background so that we can delay light display until after "attempting" to connect. This allows
    // much tighter alignment of connection anims and cube lights without the time cost of waiting to connect 
    modifiers.connectToCubeInBackground = true;
    modifiers.ensuresCubeConnectionAtDelegation = true;
  };
  virtual void GetAllDelegates(std::set<IBehavior*>& delegates) const override;
  virtual void GetBehaviorJsonKeys(std::set<const char*>& expectedKeys) const override;
  
  virtual void InitBehavior() final override;
  virtual bool WantsToBeActivatedBehavior() const override;
  virtual void OnBehaviorActivated() override;
  virtual void BehaviorUpdate() override;

  // ICubeConnectionSubscriber - - - - -
  virtual void ConnectedCallback(CubeConnectionType connectionType) override;
  virtual void ConnectionFailedCallback() override;
  virtual void ConnectionLostCallback() override;

private:

  enum class EState{
    GetIn,
    Connecting,
    ConnectionSucceeded,
    ConnectionFailed,
    Connected,
    DelegatedWithoutConnection,
    ConnectionLost,
  };

  void TransitionToConnecting();
  void TransitionToConnectionSucceeded();
  void TransitionToConnectionFailed();
  void TransitionToConnected();
  void TransitionToDelegatedWithoutConnection();
  void TransitionToConnectionClosed();
  void TransitionToConnectionLost();

  struct InstanceConfig {
    InstanceConfig();
    std::string delegateBehaviorIDString;
    std::shared_ptr<ICozmoBehavior> delegateBehavior;
    bool delegateOnFailedConnection;
  };

  struct DynamicVariables {
    DynamicVariables();
    EState state;
    bool connectedInBackground;
    bool subscribedInteractable;
    bool connectedOnLastUpdate;
    bool connectionFailedOnLastUpdate;
  };

  InstanceConfig _iConfig;
  DynamicVariables _dVars;
  
};

} // namespace Vector
} // namespace Anki

#endif // __Engine_AiComponent_BehaviorComponent_Behaviors_BehaviorConnectToCube__

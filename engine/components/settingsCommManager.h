/**
 * File: settingsCommManager.h
 *
 * Author: Paul Terry
 * Created: 6/15/18
 *
 * Description: Communicates settings with App and Cloud; calls into SettingsManager
 * (for robot settings), AccountSettingsManager and UserEntitlementsManager
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#ifndef __Cozmo_Basestation_Components_settingsCommManager_H__
#define __Cozmo_Basestation_Components_settingsCommManager_H__

#include "engine/cozmoContext.h"
#include "engine/robotComponents_fwd.h"

#include "util/entityComponent/iDependencyManagedComponent.h"
#include "util/helpers/noncopyable.h"
#include "util/signals/simpleSignal_fwd.h"

#include "proto/external_interface/messages.pb.h"
#include "proto/external_interface/settings.pb.h"

#include "json/json.h"

namespace Anki {
namespace Vector {

template <typename T>
class AnkiEvent;
class IGatewayInterface;
class SettingsManager;
class AccountSettingsManager;
class UserEntitlementsManager;
class JdocsManager;
namespace external_interface {
  class GatewayWrapper;
  class PullJdocsRequest;
  class UpdateSettingsRequest;
}

// callback signature for being notified of a volume change
using OnVolumeChangedCallback = std::function<void()>;
using SettingsReactorId = uint32_t;
// we'll have a problem if we register 4294967295 listeners
// if we were really worried we could use uuids with a larger range for ids,
// and/or check for duplicate ids when we roll over.
// But if we registered reactors at 10Hz it would take about 13 years to exhaust a uint32.


class SettingsCommManager : public IDependencyManagedComponent<RobotComponentID>,
                            private Anki::Util::noncopyable
{
public:
  struct VolumeChangeReactorHandle
  {
    SettingsReactorId id;
    OnVolumeChangedCallback callback;
  };

  SettingsCommManager();

  //////
  // IDependencyManagedComponent functions
  //////
  virtual void InitDependent(Robot* robot, const RobotCompMap& dependentComponents) override;
  virtual void GetInitDependencies(RobotCompIDSet& dependencies) const override {
    dependencies.insert(RobotComponentID::CozmoContextWrapper);
    dependencies.insert(RobotComponentID::SettingsManager);
    dependencies.insert(RobotComponentID::JdocsManager);
  };
  virtual void GetUpdateDependencies(RobotCompIDSet& dependencies) const override {
  };
  virtual void UpdateDependent(const RobotCompMap& dependentComps) override;
  //////
  // end IDependencyManagedComponent functions
  //////

  bool HandleRobotSettingChangeRequest   (const external_interface::RobotSetting robotSetting,
                                          const Json::Value& settingJson,
                                          const bool updateSettingsJdoc = false);
  bool ToggleRobotSettingHelper          (const external_interface::RobotSetting robotSetting);

  bool HandleAccountSettingChangeRequest (const external_interface::AccountSetting accountSetting,
                                          const Json::Value& settingJson,
                                          const bool updateSettingsJdoc = false);
  bool ToggleAccountSettingHelper        (const external_interface::AccountSetting accountSetting);

  bool HandleUserEntitlementChangeRequest(const external_interface::UserEntitlement userEntitlement,
                                          const Json::Value& settingJson,
                                          const bool updateUserEntitlementsJdoc = false);
  bool ToggleUserEntitlementHelper       (const external_interface::UserEntitlement userEntitlement);

  void RefreshConsoleVars();

  // for volume change notifications
  // we do notifications from settingsCommManager and not settingsManager because we're interested in being
  // notified of volume changes coming in from the app and other sources that use that interface,
  // not voice commands which (through behaviorVolume) go directly to settingsManager
  SettingsReactorId RegisterVolumeChangeReactor(OnVolumeChangedCallback callback);
  void UnRegisterVolumeChangeReactor(SettingsReactorId id);

private:

  void HandleEvents                   (const AnkiEvent<external_interface::GatewayWrapper>& event);
  void OnRequestPullJdocs             (const external_interface::PullJdocsRequest& pullJdocsRequest);
  void OnRequestUpdateSettings        (const external_interface::UpdateSettingsRequest& updateSettingsRequest);
  void OnRequestUpdateAccountSettings (const external_interface::UpdateAccountSettingsRequest& updateAccountSettingsRequest);
  void OnRequestUpdateUserEntitlements(const external_interface::UpdateUserEntitlementsRequest& updateUserEntitlementsRequest);
  void OnRequestCheckCloud            (const external_interface::CheckCloudRequest& checkCloudRequest);

  Robot*                   _robot = nullptr;
  SettingsManager*         _settingsManager = nullptr;
  AccountSettingsManager*  _accountSettingsManager = nullptr;
  UserEntitlementsManager* _userEntitlementsManager = nullptr;
  JdocsManager*            _jdocsManager = nullptr;
  IGatewayInterface*       _gatewayInterface = nullptr;

  std::vector<Signal::SmartHandle> _signalHandles;

  std::vector<VolumeChangeReactorHandle> _volumeChangeReactors;

};


} // namespace Vector
} // namespace Anki

#endif // __Cozmo_Basestation_Components_settingsCommManager_H__

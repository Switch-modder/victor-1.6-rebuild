/**
 * File: userIntentComponent.h
 *
 * Author: Brad Neuman
 * Created: 2018-02-01
 *
 * Description: Component to hold and query user intents (e.g. voice or app commands)
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#ifndef __Engine_AiComponent_UserIntentComponent_H__
#define __Engine_AiComponent_UserIntentComponent_H__

#include "engine/aiComponent/behaviorComponent/userIntentComponent_fwd.h"

#include "anki/cozmo/shared/animationTag.h"
#include "clad/cloud/mic.h"
#include "clad/robotInterface/messageEngineToRobot.h"
#include "clad/types/behaviorComponent/streamAndLightEffect.h"
#include "coretech/common/shared/types.h"
#include "engine/aiComponent/behaviorComponent/behaviorComponents_fwd.h"
#include "engine/aiComponent/behaviorComponent/behaviorTypesWrapper.h"
#include "engine/aiComponent/behaviorComponent/userIntents.h"
#include "engine/components/backpackLights/engineBackpackLightComponentTypes.h"
#include "util/entityComponent/iDependencyManagedComponent.h"
#include "util/helpers/noncopyable.h"
#include "util/signals/simpleSignal_fwd.h"

#include "json/json-forwards.h"

#include <mutex>
#include <unordered_set>
#include <list>

namespace Anki {
  
namespace Util {
  class IConsoleFunction;
}
  
namespace Vector {

enum class AnimationTrigger : int32_t;
class BackpackLightComponent;
class BehaviorComponentCloudServer;
class CozmoContext;
class Robot;
class UserIntent;
class UserIntentMap;

struct TriggerWordResponseData;

namespace ExternalInterface{
struct AppIntent;
}

namespace RobotInterface{
struct TriggerWordDetected;
}

// helper to avoid .h dependency on userIntent.clad
const UserIntentSource& GetIntentSource(const UserIntentData& intentData);

class UserIntentComponent : public IDependencyManagedComponent<BCComponentID>, private Util::noncopyable
{
public:
  
  UserIntentComponent(const Robot& robot, const Json::Value& userIntentMapConfig);

  ~UserIntentComponent();
  
  virtual void InitDependent( Vector::Robot* robot, const BCCompMap& dependentComps ) override;
  virtual void UpdateDependent(const BCCompMap& dependentComps) override;

  ////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Define how the robot should respond to hearing the trigger word and whether engine/this component
  // should respond to the trigger word being detected
  // 
  ////////////////////////////////////////////////////////////////////////////////////////////////////////////
  void StartWakeWordlessStreaming( CloudMic::StreamType streamType, bool playGetInFromAnimProcess = false );
  
  // Define how the user intent component should respond to the trigger word being detected
  void DisableEngineResponseToTriggerWord(const std::string& disablerName, bool disable);
  bool GetEngineShouldRespondToTriggerWord() const { return _disableTriggerWordNames.empty(); }
  bool IsTriggerWordDisabledByName(const std::string& disablerName) const 
  { 
    return _disableTriggerWordNames.find(disablerName) != _disableTriggerWordNames.end(); 
  }

  // The animation process handles the initial response when a trigger word is detected - the exact response is
  // defined by the top SetTriggerWordResponse message in the stack
  // When responses are popped by id, if it is the top response in the stack the new top of the stack will be sent to the
  // anim process. Otherwise no user facing change will occur
  
  // Note that the animationTrigger passed in here does not follow traditional trigger/group conventions regarding
  // animations being re-selected each time one is played. A single animation is selected from the group when this
  // function is called and that animation will persist until this function is called again with the same id/trigger
  void PushResponseToTriggerWord(const std::string& id, const TriggerWordResponseData& newState);
  void PushResponseToTriggerWord(const std::string& id, const AnimationTrigger& getInAnimTrigger, 
                                 const AudioEngine::Multiplexer::PostAudioEvent& postAudioEvent = {},
                                 StreamAndLightEffect streamAndLightEffect = StreamAndLightEffect::StreamingDisabled,
                                 int32_t minStreamingDuration_ms = -1);
  void PushResponseToTriggerWord(const std::string& id, const std::string& getInAnimationName = "", 
                                 const AudioEngine::Multiplexer::PostAudioEvent& postAudioEvent = {},
                                 StreamAndLightEffect streamAndLightEffect = StreamAndLightEffect::StreamingDisabled,
                                 int32_t minStreamingDuration_ms = -1);
  void PopResponseToTriggerWord(const std::string& id);

  // Copies the current response to the trigger word but overrides the shouldStream and ShouldSimulateStream
  // to match the desired StreamAndLightEffect
  void AlterStreamStateForCurrentResponse(const std::string& id, const StreamAndLightEffect newEffect);


  bool WaitingForTriggerWordGetInToFinish() const { return _waitingForTriggerWordGetInToFinish; }


  ////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Trigger word:
  //
  // The trigger word will be "pending" any time the audio process reports it. It will stay pending until it
  // is cleared, which should be very quickly in order to have a fast reaction. If a second trigger word comes
  // in (which should not happen and will likely cause a warning), they do _not_ queue, so it'll still just
  // count as pending and get cleared by a single clear
  ////////////////////////////////////////////////////////////////////////////////////////////////////////////
  
  // Returns true if the trigger / wake word is pending
  bool IsTriggerWordPending() const;

  // if trigger word is pending, return whether or not the anim process will automatically open a stream
  bool WillPendingTriggerWordOpenStream() const { return _pendingTriggerWillStream; }
  
  // Clear the pending trigger word. All subsequent calls to IsTriggerWordPending will return false until
  // another trigger word comes in.
  void ClearPendingTriggerWord();
  
  // Sets the trigger word as pending if there is an engine response.
  // muteEdgeCase: if the trigger word started from mute, then the behavior stack will be in Wait,
  // with no trigger word response, until a couple ticks after mute ends. In this case, wait longer
  // before the pending trigger expires and ignore any previous calls to DisableEngineResponseToTriggerWord().
  // TODO (VIC-11795): no muteEdgeCase param
  void SetTriggerWordPending(const bool willOpenStream, const bool muteEdgeCase = false);

  ////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // User Intent:
  //
  // A user intent is an enum member defined in userIntent.clad. It can come from a voice command but also
  // elsewhere (e.g. from the app once that is supported). Intents, like trigger words, should generally be
  // handled immediately, and they do not queue. If another intent comes in while the last one is still
  // pending, it will be overwritten. There is up to one _pending_ intent and one _active_ intent at any time
  ////////////////////////////////////////////////////////////////////////////////////////////////////////////

  // Returns true if any user intent is pending
  bool IsAnyUserIntentPending() const;

  // Returns true if the specific user intent is pending
  bool IsUserIntentPending(UserIntentTag userIntent) const;

  // Same as above, but also get data
  bool IsUserIntentPending(UserIntentTag userIntent, UserIntent& extraData) const;

  // Activate the pending user intent. Owner string is stored for debugging because you are responsible to
  // deactivate the intent if you call this function directly. If the given userIntent is pending, this will
  // move it from pending to active (it will no longer be pending) and return a pointer to the full intent
  // data. Otherwise, it will print warnings and return nullptr
  // showFeedback = true will cause Vector to actively display to the user that he is carrying out this intent
  // autoShutoffFeedback = true will cause the feedback to shut off after a certain amount of time
  UserIntentPtr ActivateUserIntent(UserIntentTag userIntent, const std::string& owner, bool showFeedback, bool autoShutoffFeedback = false);

  // Must be called when a user intent is no longer active (generally meaning that the behavior that was
  // handling the intent has stopped).
  void DeactivateUserIntent(UserIntentTag userIntent);

  // there is a period of time between getting an intent back and and actually activating it and this let's us know
  // that we expect the intent to become active, so start the transition
  void StartTransitionIntoActiveUserIntentFeedback();

  // The "active user intent state" is used to convey to the user that Vector is actively carrying out the user's
  // voice intent, which is triggered by passing in showFeedback = true to ActivateUserIntent(...)
  // This funciton allows us to stop displaying this feedback to the user, but still keeping the user
  // intent active (userfull when sleeping, etc)
  void StopActiveUserIntentFeedback();

  // Check if an intent tag is currently active
  bool IsUserIntentActive(UserIntentTag userIntent) const;
  // Check if ANY intent is currently active
  bool IsAnyUserIntentActive() const { return static_cast<bool>(GetActiveUserIntent()); }

  // If the given intent is active, return the associated data, otherwise return nullptr.
  UserIntentPtr GetUserIntentIfActive(UserIntentTag forIntent) const;

  // If any intent is active, return the associated data, otherwise return nullptr. In general, most cases
  // should use the above version that checks that the explicit user intent is matching what the caller
  // expects
  UserIntentPtr GetActiveUserIntent() const { return _activeIntent; }
  
  // You really really really shouldn't use this. You should almost always be able to check against
  // a specific intent, or GetActiveUserIntent(). 
  const UserIntentData* /* DONT USE THIS */ GetPendingUserIntent() const { return _pendingIntent.get(); }

  // A helper function to drop a user intent without responding to it. This is meant to be called for a
  // pending user intent and will make it no longer pending without ever making it active. This is generally
  // not what you want, but can be useful to explicitly ignore a command
  void DropUserIntent(UserIntentTag userIntent);
  
  // Drops any pending user intents without responding to them. This is generally not what you want
  // for any behavior. Use the above if needed
  void DropAnyUserIntent();

  // returns if an intent has recently gone unclaimed. or, reset the corresponding flag
  bool WasUserIntentUnclaimed() const { return _wasIntentUnclaimed; };
  void ResetUserIntentUnclaimed() { _wasIntentUnclaimed = false; };

  // returns true if there has been an error receiving data from the cloud (e.g. no internet, couldn't reach
  // chipper). Must be manually reset with the reset call below
  bool WasUserIntentError() const { return _wasIntentError; };
  void ResetUserIntentError() { _wasIntentError = false; };

  bool IsCloudStreamOpen() const { return _isStreamOpen; }

  // replace the current pending user intent (if any) with this one. This will assert in dev if the user
  // intent data type is not void. These should only be used for test and dev purposes (e.g. webviz), not
  // directly used to express an intent
  void DevSetUserIntentPending(UserIntentTag userIntent, const UserIntentSource& source);
  void DevSetUserIntentPending(UserIntent&& userIntent, const UserIntentSource& source);
  void DevSetUserIntentPending(UserIntentTag userIntent);
  void DevSetUserIntentPending(UserIntent&& userIntent);

  // set/clear whitelisted intents. Everything else will be unmatched. For now this is only tags since
  // the only use case is tags, not full UserIntent objects
  void SetWhitelistedIntents( const std::set<UserIntentTag>& whitelisted ) { _whitelistedIntents = whitelisted; }
  void ClearWhitelistedIntents() { _whitelistedIntents.clear(); }

  // this allows us to temporarilty disable the warning when we haven't responded to a pending intent
  // useful when we know a behavior down the line will consume the intent, but we still
  // have some work to do at the moment
  // note: this is re-enabled with each new intent
  void SetUserIntentTimeoutEnabled(bool isEnabled);
  

  ////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Cloud Intents:
  //
  // A cloud intent is a user intent sent from cloud. In normal operation, these come from the vic-cloud
  // process as a CloudMic::Message CLAD message. There are multiple types of messages, some which signify
  // errors. All valid intents are of type "result". The CLAD message contains a key called "parameters"
  // (since "params" is reserved in CLAD) which contains a stringified version of the JSON parameters. This is
  // then converted into an "expanded" json value, which uses the key "params" instead of "parameters" and has
  // full JSON for any parameters
  ////////////////////////////////////////////////////////////////////////////////////////////////////////////

  // convert the cloud JSON object into a user intent, mapping to the default intent if no cloud intent
  // matches. Returns true if successfully converted (including if it matched against the default), and false
  // if there was an error (e.g. malformed json, incorrect data fields, etc)
  bool SetIntentPendingFromCloudJSONValue(Json::Value cloudJson);

  // Set a cloud intent CLAD struct pending based on a CloudMic message in string json format (e.g. pasted
  // from WebnViz or console output). Returns true if successfully set. Supports specifying a "type"
  // (e.g. "result" or "error"), but assumes "result" if not specified. NOTE: this expects any parameters to
  // be stringified in a "parameters" field
  bool SetCloudIntentPendingFromString(const std::string& cloudStr);

  // Set a cloud intent JSON value fully expanded from a string. Used primarily in unit tests, this expects
  // any params to be fully expanded as json in "params".
  bool SetCloudIntentPendingFromExpandedJSON(const std::string& expandedJson);
  
  // get list of cloud/app intents from json
  std::vector<std::string> DevGetCloudIntentsList() const;
  std::vector<std::string> DevGetAppIntentsList() const;

private:
  
  // callback from the cloud
  void OnCloudData(CloudMic::Message&& data);
  
  // message received from app
  void OnAppIntent( const ExternalInterface::AppIntent& appIntent );

  // helper to apply a "stream and light" effect to the given trigger word response message
  void ApplyStreamAndLightEffect(const StreamAndLightEffect streamAndLightEffect,
                                 RobotInterface::SetTriggerWordResponse& message);
  
  std::string GetServerName(const Robot& robot) const;
  
  void SendWebVizIntents();
  
  void SetUserIntentPending(UserIntentTag userIntent, const UserIntentSource& source);
  void SetUserIntentPending(UserIntent&& userIntent, const UserIntentSource& source);

  void PushResponseToTriggerWordInternal(const std::string& id, RobotInterface::SetTriggerWordResponse&& response);
  bool HasAnimResponseToTriggerWord() const;

  void HandleTriggerWordEventForDas(const RobotInterface::TriggerWordDetected& msg);
  
  void SetupConsoleFuncs();

  static size_t sActivatedIntentID;

  Robot*                    _robot = nullptr;
  std::unique_ptr<UserIntentMap> _intentMap;

  bool _pendingTrigger = false;
  bool _pendingTriggerWillStream = false;
  
  std::unique_ptr<UserIntentData> _pendingIntent;
  std::shared_ptr<UserIntentData> _activeIntent;
  std::string _activeIntentOwner;

  // super lightweight class for tracking the "active user state" which is how we display to the user that Vector
  // is actively responding to their voice intent
  // kept separate from _activeIntent since they are not necessary 1-to-1, but are very interconnected
  class ActiveIntentFeedback
  {
    public:
      ActiveIntentFeedback();
      void Init(Robot* robot);

      void StartTransitionIntoActive();
      void StopTransitionIntoActive();

      // activating/deactivating this state will cause vector to convey to the user that he is responding to an active user intent
      void Activate(UserIntentTag userIntent, bool autoShutoff);
      void Deactivate(UserIntentTag userIntent);
      void Update();

    private:
      bool IsEnabled() const;
      bool IsActive() const;

      Robot* robot;
      UserIntentTag activatedIntentTag;
      BackpackLightDataLocator activeLightsHandle;

      bool isTransitionActive = false;
      float transitionShutOffTime = 0.0f;
      float feedbackShutOffTime = 0.0f;
  } _activeIntentFeedback;

  // for debugging -- intents should be processed within n ticks so track the ticks here
  size_t _pendingTriggerTimeout = 0; // expiration tick
  size_t _pendingIntentTick = 0; // occurrence tick
  bool _pendingIntentTimeoutEnabled = true;
  
  // holds cloud and trigger word event handles
  std::vector<::Signal::SmartHandle> _eventHandles;
  
  std::mutex _mutex;
  CloudMic::Message _pendingCloudIntent; // only pending for as long as it takes this thread to obtain a lock
  std::unique_ptr<BehaviorComponentCloudServer> _server;
  
  bool _wasIntentUnclaimed = false;
  bool _wasIntentError = false;
  bool _isStreamOpen = false;
  
  // if non-empty, only these cloud/app intents will match a user intent
  std::set<UserIntentTag> _whitelistedIntents;
  
  const CozmoContext* _context = nullptr; // for webviz
  
  std::string _devLastReceivedCloudIntent;
  std::string _devLastReceivedAppIntent;
  
  std::list<Anki::Util::IConsoleFunction> _consoleFuncs;

  std::unordered_set<std::string> _disableTriggerWordNames;

  // Keep track of who has requested to disable streaming-after-wakeword
  struct TriggerWordResponseEntry{
    TriggerWordResponseEntry(const std::string& id, RobotInterface::SetTriggerWordResponse&& responseExternal)
    : setID(id)
    , response(responseExternal){}
    std::string setID;
    RobotInterface::SetTriggerWordResponse response;
  };

  std::vector<TriggerWordResponseEntry> _responseToTriggerWordMap;
  AnimationTag _tagForTriggerWordGetInCallbacks;
  bool _waitingForTriggerWordGetInToFinish = false;
  float _waitingForTriggerWordGetInToFinish_setTime_s = 0.0f;

};

}
}


#endif

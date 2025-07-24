/**
 * File: beatDetectorComponent
 *
 * Author: Matt Michini
 * Created: 05/03/2018
 *
 * Description: Component for managing beat/tempo detection (which is actually done in the anim process)
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#ifndef __Engine_Components_BeatDetectorComponent_H_
#define __Engine_Components_BeatDetectorComponent_H_

#include "engine/robotComponents_fwd.h"

#include "clad/types/beatDetectorTypes.h"

#include "util/entityComponent/iDependencyManagedComponent.h"
#include "util/signals/simpleSignal_fwd.h"

#include <deque>

namespace Anki {
namespace Vector {

class Robot;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
class BeatDetectorComponent : public IDependencyManagedComponent<RobotComponentID>
{
public:
  
  BeatDetectorComponent();
  ~BeatDetectorComponent();
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // IDependencyManagedComponent functions
  
  virtual void GetInitDependencies( RobotCompIDSet& dependencies ) const override;
  virtual void GetUpdateDependencies( RobotCompIDSet& dependencies ) const override {};
  
  virtual void InitDependent(Vector::Robot* robot, const RobotCompMap& dependentComps) override;
  virtual void UpdateDependent(const RobotCompMap& dependentComps) override;
  
  // Is there _potentially_ a steady musical beat happening? Even if this returns true, it is possible that it is a
  // false positive detection. This should be confirmed by calling IsBeatDetected() after some 'listening' time.
  bool IsPossibleBeatDetected() const;
  
  // Is there _definitely_ a steady musical beat happening?
  bool IsBeatDetected() const;
  
  // Predicted time (UniversalTime) of the next beat. Returns 0 and prints a warning if there is no 'possible' beat
  // detected right now
  float GetNextBeatTime_sec() const;

  // Get the current detected tempo. Returns 0 and prints a warning if there is no 'possible' beat detected right now
  float GetCurrTempo_bpm() const;
  
  // Get the current beat confidence, which is a unitless measure of how confident the beat detection algorithm is.
  // Returns 0 and prints a warning if there is no 'possible' beat detected right now
  float GetCurrConfidence() const;
  
  // Send a message to reset the beat detector in the anim process and clear our list of recent beats
  void Reset();
  
  using OnBeatCallback = std::function<void(void)>;
  
  // OnBeatCallbacks get called when we receive a 'beat' message
  // from the anim process, i.e. we have new beat information.
  // Returns a unique indentifier to allow un-registering. The
  // identifier is always a positive number.
  int RegisterOnBeatCallback(const OnBeatCallback& callback);
  
  // Returns true if the callback existed and was unregistered.
  bool UnregisterOnBeatCallback(const int callbackId);
  
  void SendToWebViz();
  
private:
  
  // Called when we receive a beat message from the anim process
  void OnBeat(const BeatInfo& beat);
  
  BeatInfo TEST_fakeLowConfidenceBeat(const float tempo_bpm, const float time_sec);
  BeatInfo TEST_fakeHighConfidenceBeat(const float tempo_bpm, const float time_sec);
  BeatInfo TEST_fakeVeryHighConfidenceBeat(const float tempo_bpm, const float time_sec);
  
  Robot* _robot = nullptr;
  
  // Anim message event handler
  Signal::SmartHandle _signalHandle;
  
  // Store recent beat events
  std::deque<BeatInfo> _recentBeats;
  
  // map of unique ID to OnBeatCallback
  std::map<int, OnBeatCallback> _onBeatCallbacks;
  
  float _nextSendWebVizDataTime_sec = -1.f;
};

} // namespace Vector
} // namespace Anki

#endif // __Engine_Components_BeatDetectorComponent_H_

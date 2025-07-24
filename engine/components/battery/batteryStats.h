/**
 * File: batteryStats.h
 *
 * Author: Matt Michini
 * Created: 9/5/2018
 *
 * Description: Gather and record some long-running battery statistics
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#ifndef __Engine_Components_BatteryStats_H__
#define __Engine_Components_BatteryStats_H__

#include <limits>
#include <memory>

namespace Anki {
namespace Util{
namespace Stats{
  class StatsAccumulator;
}
}
namespace Vector {

class BatteryStats
{
public:
  BatteryStats();
  ~BatteryStats();
  
  void Update(const float batteryTemp_degC, const float batteryVolts);

  // TODO: Mooching off of this accumulator to report encoder power stats out of laziness.
  //       Move to somewhere else!
  void UpdateEncoderStats(bool encodersDisabled, bool calmMode);
  
private:
  // Write a DAS event with the current statistics.
  // Note: This will clear the stats accumulator(s) when called.
  void LogToDas();
  
  std::unique_ptr<Util::Stats::StatsAccumulator> _temperatureStats_degC;
  std::unique_ptr<Util::Stats::StatsAccumulator> _voltageStats;
  
  float _lastSampleTime_sec = 0.f;
  float _lastDasSendTime_sec = 0.f;

  unsigned int _encoderStateSamples = 0;
  unsigned int _encoderDisabledCount = 0;
  unsigned int _encoderCalmCount = 0;
};
  

} // Vector namespace
} // Anki namespace

#endif // __Engine_Components_BatteryStats_H__

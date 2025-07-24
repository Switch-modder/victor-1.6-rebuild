/**
 * File:        hal_imu.cpp
 *
 * Author:      Kevin Yoon
 * Created:     05/26/2017
 *
 * Description: IMU interface via SPI
 *
 **/

#include "anki/cozmo/robot/spi_imu.h"

// Our Includes
#include "anki/cozmo/robot/DAS.h"
#include "anki/cozmo/robot/logging.h"
#include "anki/cozmo/robot/hal.h"
#include "anki/cozmo/shared/factory/faultCodes.h"

#include "clad/robotInterface/messageRobotToEngine.h"
#include "clad/robotInterface/messageRobotToEngine_send_helper.h"

#ifdef IMU_DEBUG
#include "hdr_histogram.h"
#include "hdr_histogram_log.h"
#endif

#include <thread>
#include <mutex>

namespace Anki {
namespace Vector {

namespace { // "Private members"

  const int IMU_DATA_ARRAY_SIZE = 5;
  HAL::IMU_DataStructure _imuDataArr[IMU_DATA_ARRAY_SIZE];
  u8 _imuLastReadIdx = 0;
  u8 _imuNewestIdx = 0;

  // How often, in ticks, to update the imu tempurature
  const u32 IMU_TEMP_UPDATE_FREQ_TICKS = 200; // ~1 second

  // IMU interactions are handled on a thread due to blocking system calls
#if PROCESS_IMU_ON_THREAD
  std::thread _processor;
  std::mutex _mutex;
  bool _stopProcessing = false;
#endif

} // "private" namespace


void PushIMU(const HAL::IMU_DataStructure& data)
{
#if PROCESS_IMU_ON_THREAD
  std::lock_guard<std::mutex> lock(_mutex);
#endif
  if (++_imuNewestIdx >= IMU_DATA_ARRAY_SIZE) {
    _imuNewestIdx = 0;
  }
  if (_imuNewestIdx == _imuLastReadIdx) {
    AnkiWarn( "HAL.PushIMU.ArrayIsFull", "Dropping data");
  }

  _imuDataArr[_imuNewestIdx] = data;
}

bool PopIMU(HAL::IMU_DataStructure& data)
{
#if PROCESS_IMU_ON_THREAD
  std::lock_guard<std::mutex> lock(_mutex);
#endif

  if (_imuNewestIdx == _imuLastReadIdx) {
    return false;
  }

  if (++_imuLastReadIdx >= IMU_DATA_ARRAY_SIZE) {
    _imuLastReadIdx = 0;
  }

  data = _imuDataArr[_imuLastReadIdx];
  return true;
}

void ProcessIMUEvents()
{
  static u8 tempCount = 0;
  if(tempCount++ >= IMU_TEMP_UPDATE_FREQ_TICKS)
  {
    tempCount = 0;
    imu_update_temperature();
  }

  IMURawData rawData[IMU_MAX_SAMPLES_PER_READ];
  HAL::IMU_DataStructure imuData;
  const int imu_read_samples = imu_manage(rawData);
  if(imu_read_samples < 0)
  {
    AnkiError("HAL.ProcessIMUEvents.IMUManageFailed", "");

    static bool sentDAS = false;
    if(!sentDAS)
    {
      sentDAS = true;
      DASMSG(imu_failure,
             "robot.imu_failure",
             "Indicates that we failed to read/write to the IMU and it may not be working correctly");
      DASMSG_SEND_ERROR();
    }
  }

  for (int i=0; i < imu_read_samples; i++) {
    for (int j=0 ; j<3 ; j++) {
      imuData.accel[j] = rawData[i].acc[j]  * IMU_ACCEL_SCALE_G  * MMPS2_PER_GEE;
      imuData.gyro[j]  = rawData[i].gyro[j] * IMU_GYRO_SCALE_DPS * RADIANS_PER_DEGREE;
    }
    imuData.temperature_degC = IMU_TEMP_RAW_TO_C(rawData[i].temperature);
    PushIMU(imuData);
  }
}

bool OpenIMU()
{
  const char* err = imu_open();
  if (err) {
    AnkiError("HAL.InitIMU.OpenFailed", "%s", err);
    FaultCode::DisplayFaultCode(FaultCode::IMU_FAILURE);
    return false;
  }
  imu_init();
  return true;
}

#if PROCESS_IMU_ON_THREAD
// Processing loop for reading imu on a thread
void ProcessLoop()
{
#ifdef IMU_DEBUG
  const s32 IMU_HDR_GRANULARITY = 1;
  const double IMU_HDR_MULTIPLIER = 1.0;
  const u32 IMU_HDR_RATE_LIMIT = 1000;
  const u32 IMU_HDR_SIG_FIG = 1;
  const u32 IMU_HDR_MIN_VALUE = 1;
  const u32 IMU_HDR_MAX_VALUE = 30000;
  struct hdr_histogram *_hdr;
  u32 hdr_print_rate_limit = 0;

  hdr_init(IMU_HDR_MIN_VALUE,
           IMU_HDR_MAX_VALUE,
           IMU_HDR_SIG_FIG,
           &_hdr);
#endif

  if(!OpenIMU())
  {
    return;
  }

  while(!_stopProcessing)
  {
    const auto start = std::chrono::steady_clock::now();
    ProcessIMUEvents();
    const auto end = std::chrono::steady_clock::now();

    // Sleep such that there are 5ms between ProcessIMUEvent calls
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::chrono::duration<double, std::micro> sleepTime = std::chrono::milliseconds(5) - elapsed;
    std::this_thread::sleep_for(sleepTime);

#ifdef IMU_DEBUG
    hdr_record_value(_hdr, elapsed.count());

    if(!(++hdr_print_rate_limit % IMU_HDR_RATE_LIMIT)) {
      hdr_percentiles_print(_hdr, stdout, IMU_HDR_GRANULARITY,
                            IMU_HDR_MULTIPLIER, CLASSIC);
      hdr_print_rate_limit = 0;
    }
#endif
  }

  imu_close();
}
#endif

void InitIMU()
{

#if PROCESS_IMU_ON_THREAD
  // Spin up the processing thread and detach it
  // This will open, init, and read the imu
  _processor = std::thread(ProcessLoop);
#else
  OpenIMU();
#endif
}

void StopIMU()
{
#if PROCESS_IMU_ON_THREAD
  _stopProcessing = true;
  if (_processor.joinable()) {
    _processor.join();
  }
#else
  imu_close();
#endif
  AnkiInfo("HAL.StopIMU.Stopped", "");
}


bool HAL::IMUReadData(HAL::IMU_DataStructure &imuData)
{
#if(0) // For faking IMU data
  while (PopIMU(imuData)) {}; // Just to pop queue
  static TimeStamp_t lastIMURead = 0;
  TimeStamp_t now = HAL::GetTimeStamp();
  if (now - lastIMURead > 4) {
    // TEMP HACK: Send 0s because on my Nexus 5x, the gyro values are kinda crazy.
    imuData.accel[0] = 0.f;
    imuData.accel[1] = 0.f;
    imuData.accel[2] = 9800.f;
    imuData.gyro[0] = 0.f;
    imuData.gyro[1] = 0.f;
    imuData.gyro[2] = 0.f;

    lastIMURead = now;
    return true;
  }
  return false;
#else
  return PopIMU(imuData);
#endif
}

} // namespace Vector
} // namespace Anki

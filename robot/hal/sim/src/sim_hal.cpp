/**
 * File: sim_hal.cpp
 *
 * Author: Andrew Stein (andrew)
 * Created: 10/10/2013
 * Author: Daniel Casner <daniel@anki.com>
 * Revised for Cozmo 2: 02/27/2017
 *
 * Description:
 *
 *   Simulated HAL implementation for Cozmo 2.0
 *
 *   This is an "abstract class" defining an interface to lower level
 *   hardware functionality like getting an image from a camera,
 *   setting motor speeds, etc.  This is all the Robot should
 *   need to know in order to talk to its underlying hardware.
 *
 *   To avoid C++ class overhead running on a robot's embedded hardware,
 *   this is implemented as a namespace instead of a fullblown class.
 *
 *   This just defines the interface; the implementation (e.g., Real vs.
 *   Simulated) is given by a corresponding .cpp file.  Which type of
 *   is used for a given project/executable is decided by which .cpp
 *   file gets compiled in.
 *
 * Copyright: Anki, Inc. 2017
 *
 **/


// System Includes
#include <array>
#include <cassert>
#include <cmath>
#include <cstdlib>

// Our Includes
#include "anki/cozmo/robot/logging.h"
#include "anki/cozmo/robot/hal.h"
#include "anki/cozmo/shared/cozmoConfig.h"
#include "audioUtil/audioCaptureSystem.h"
#include "util/container/fixedCircularBuffer.h"
#include "util/logging/logging.h"
#include "util/math/math.h"
#include "util/math/numericCast.h"
#include "messages.h"
#include "wheelController.h"

#include "simulator/robot/sim_overlayDisplay.h"
#include "clad/robotInterface/messageRobotToEngine_send_helper.h"

// Webots Includes
#include <webots/Robot.hpp>
#include <webots/Supervisor.hpp>
#include <webots/PositionSensor.hpp>
#include <webots/Motor.hpp>
#include <webots/GPS.hpp>
#include <webots/Compass.hpp>
#include <webots/Display.hpp>
#include <webots/Gyro.hpp>
#include <webots/DistanceSensor.hpp>
#include <webots/Accelerometer.hpp>
#include <webots/Receiver.hpp>
#include <webots/Connector.hpp>
#include <webots/LED.hpp>

#define DEBUG_GRIPPER 0

// Whether or not to simulate gyro bias and drift due to temperature
static const bool kSimulateGyroBias = false;

#ifndef SIMULATOR
#error SIMULATOR should be defined by any target using sim_hal.cpp
#endif

namespace Anki {
  namespace Vector {

    namespace { // "Private members"

      // Const paramters / settings
      // TODO: some of these should be defined elsewhere (e.g. comms)

      const f64 WEBOTS_INFINITY = std::numeric_limits<f64>::infinity();

      constexpr auto MOTOR_LEFT_WHEEL = EnumToUnderlyingType(MotorID::MOTOR_LEFT_WHEEL);
      constexpr auto MOTOR_RIGHT_WHEEL = EnumToUnderlyingType(MotorID::MOTOR_RIGHT_WHEEL);
      constexpr auto MOTOR_LIFT = EnumToUnderlyingType(MotorID::MOTOR_LIFT);
      constexpr auto MOTOR_HEAD = EnumToUnderlyingType(MotorID::MOTOR_HEAD);
      constexpr auto MOTOR_COUNT = EnumToUnderlyingType(MotorID::MOTOR_COUNT);

      constexpr auto NUM_BACKPACK_LEDS = EnumToUnderlyingType(LEDId::NUM_BACKPACK_LEDS);


#pragma mark --- Simulated HardwareInterface "Member Variables" ---

      bool isInitialized = false;

      u32 tickCnt_ = 0; // increments each robot step (ROBOT_TIME_STEP_MS)

      webots::Supervisor webotRobot_;

      s32 robotID_ = -1;

      // Power
      HAL::PowerState powerState_ = HAL::POWER_MODE_ACTIVE;

      // Motors
      webots::Motor* leftWheelMotor_;
      webots::Motor* rightWheelMotor_;

      webots::Motor* headMotor_;
      webots::Motor* liftMotor_;

      webots::Motor* motors_[MOTOR_COUNT];

      // Motor position sensors
      webots::PositionSensor* leftWheelPosSensor_;
      webots::PositionSensor* rightWheelPosSensor_;
      webots::PositionSensor* headPosSensor_;
      webots::PositionSensor* liftPosSensor_;
      webots::PositionSensor* motorPosSensors_[MOTOR_COUNT];

      // Gripper
      webots::Connector* con_;
      bool isGripperEnabled_ = false;

      // For pose information
      webots::GPS* gps_;
      webots::Compass* compass_;

      // IMU
      webots::Gyro* gyro_;
      webots::Accelerometer* accel_;

      // Prox sensors
      webots::DistanceSensor *proxCenter_;
      webots::DistanceSensor *cliffSensors_[HAL::CLIFF_COUNT];

      // Charge contact
      webots::Connector* chargeContact_;
      bool wasOnCharger_ = false;

      // Backpack button
      webots::Field* backpackButtonPressedField_ = nullptr;

      // Touch sensor
      webots::Field* touchSensorTouchedField_;

      // For tracking wheel distance travelled
      f32 motorPositions_[MOTOR_COUNT];
      f32 motorPrevPositions_[MOTOR_COUNT];
      f32 motorSpeeds_[MOTOR_COUNT];
      f32 motorSpeedCoeffs_[MOTOR_COUNT];

      // Lights
      webots::LED* leds_[NUM_BACKPACK_LEDS] = {0};

      webots::LED* sysLed_ = nullptr;
      
      // Simulated Battery
      webots::Field *batteryVoltsField_ = nullptr;
      const webots::Field *batteryChargeRateField_ = nullptr;
      const webots::Field *batteryDischargeRateField_ = nullptr;

      const u32 batteryUpdateRate_tics_ = 50; // How often to update the simulated battery voltage (in robot ticks)

      // MicData
      // Use the mac mic as input with AudioCaptureSystem
      constexpr uint32_t kSamplesPerChunk = 80;
      constexpr uint32_t kSampleRate_hz = 16000;
      AudioUtil::AudioCaptureSystem audioCaptureSystem_(kSamplesPerChunk, kSampleRate_hz);

      // Limit the number of messages that can be sent per robot tic
      // and toss the rest. Since audio data is generated in
      // real-time vs. robot time, if Webots processes slow down or
      // are paused (debugger) then we'll end up flooding the send buffer.
      constexpr uint32_t kMicSendWindow_tics = 6;   // Roughly equivalent to every anim process tic
      constexpr uint32_t kMaxNumMicMsgsAllowedPerSendWindow = 100;
      int _numMicMsgsSent      = 0;        // Num of mic msgs sent in the current window
      int _micSendWindowTicIdx = 0;        // Which tic of the current window we're in

      constexpr uint32_t kInterleavedSamplesPerChunk = kSamplesPerChunk * 4;
      using RawChunkArray = std::array<int16_t, kInterleavedSamplesPerChunk>;
      Util::FixedCircularBuffer<RawChunkArray, kMicSendWindow_tics * kMaxNumMicMsgsAllowedPerSendWindow> micData_;
      std::mutex micDataMutex_;

#pragma mark --- Simulated Hardware Interface "Private Methods" ---
      // Localization
      //void GetGlobalPose(f32 &x, f32 &y, f32& rad);


      // Approximate open-loop conversion of wheel power to angular wheel speed
      float WheelPowerToAngSpeed(float power)
      {
        // Inverse of speed-power formula in WheelController
        float speed_mm_per_s = power / 0.004f;

        // Return linear speed m/s when usingTreads
        return -speed_mm_per_s / 1000.f;
      }

      // Approximate open-loop conversion of lift power to angular lift speed
      float LiftPowerToAngSpeed(float power)
      {
        // Inverse of speed-power formula in LiftController
        float rad_per_s = power / 0.05f;
        return rad_per_s;
      }

      // Approximate open-loop conversion of head power to angular head speed
      float HeadPowerToAngSpeed(float power)
      {
        return power * 2*M_PI_F;
      }


      void MotorUpdate()
      {
        // Update position and speed info
        f32 posDelta = 0;
        for(int i = 0; i < MOTOR_COUNT; i++)
        {
          if (motors_[i]) {
            f32 pos = motorPosSensors_[i]->getValue();
            if (i == MOTOR_LEFT_WHEEL || i == MOTOR_RIGHT_WHEEL) {
              pos = motorPosSensors_[i]->getValue() * -1000.f;
            }

            posDelta = pos - motorPrevPositions_[i];

            // Update position
            motorPositions_[i] += posDelta;

            // Update speed
            motorSpeeds_[i] = (posDelta * ONE_OVER_CONTROL_DT) * (1.0 - motorSpeedCoeffs_[i]) + motorSpeeds_[i] * motorSpeedCoeffs_[i];

            motorPrevPositions_[i] = pos;
          }
        }
      }

      void UpdateSimBatteryVolts()
      {
        // Grab the charge rate
        const auto* chargeRateField = HAL::BatteryIsCharging() ?
                                      batteryChargeRateField_ :
                                      batteryDischargeRateField_;
        const float batteryIncreaseRate_voltsPerMin = chargeRateField->getSFFloat();

        // Compute delta volts
        const float updateTime_sec = Util::MilliSecToSec((float) batteryUpdateRate_tics_ * ROBOT_TIME_STEP_MS);
        const float batteryDeltaVolts = (batteryIncreaseRate_voltsPerMin / 60.f) * updateTime_sec;
        float batteryVolts = batteryVoltsField_->getSFFloat() + batteryDeltaVolts;

        // Clamp to logical voltages
        const float minBatteryVolts = 3.4f;
        const float maxBatteryVolts = 4.21f;
        batteryVolts = Util::Clamp(batteryVolts,
                                   minBatteryVolts,
                                   maxBatteryVolts);

        batteryVoltsField_->setSFFloat(batteryVolts);
      }

      void AudioInputCallback(const AudioUtil::AudioSample* data, uint32_t numSamples)
      {
        std::lock_guard<std::mutex> lock(micDataMutex_);
        micData_.push_back();
        auto* newData = micData_.back().data();

        // Duplicate our mono channel input across 4 interleaved channels to simulate 4 mics
        constexpr int kNumChannels = 4;
        for (int j=0; j<kSamplesPerChunk; j++)
        {
          auto* sampleStart = newData + (j * kNumChannels);
          const auto sample = data[j];

          for(int i=0; i<kNumChannels; ++i)
          {
            sampleStart[i] = sample;
          }
        }
      }

      void AudioInputUpdate()
      {
        // Reset send counter for next send window
        if (++_micSendWindowTicIdx >= kMicSendWindow_tics) {
          _micSendWindowTicIdx = 0;
          _numMicMsgsSent = 0;
        }
      }

    } // "private" namespace

    namespace Sim {
      // Create a pointer to the webots supervisor object within
      // a Simulator namespace so that other Simulation-specific code
      // can talk to it.  This avoids there being a global gCozmoBot
      // running around, accessible in non-simulator code.
      webots::Supervisor* CozmoBot = &webotRobot_;
    }

#pragma mark --- Simulated Hardware Method Implementations ---

    // Forward Declaration
    Result InitRadio();

    Result HAL::Init(const int * shutdownSignal)
    {
      assert(ROBOT_TIME_STEP_MS >= webotRobot_.getBasicTimeStep());

      leftWheelMotor_  = webotRobot_.getMotor("LeftWheelMotor");
      rightWheelMotor_ = webotRobot_.getMotor("RightWheelMotor");

      headMotor_  = webotRobot_.getMotor("HeadMotor");
      liftMotor_  = webotRobot_.getMotor("LiftMotor");

      leftWheelPosSensor_ = webotRobot_.getPositionSensor("LeftWheelMotorPosSensor");
      rightWheelPosSensor_ = webotRobot_.getPositionSensor("RightWheelMotorPosSensor");

      headPosSensor_ = webotRobot_.getPositionSensor("HeadMotorPosSensor");
      liftPosSensor_ = webotRobot_.getPositionSensor("LiftMotorPosSensor");

      con_ = webotRobot_.getConnector("gripperConnector");


      // Get ID
      const auto* robotIDField = webotRobot_.getSelf()->getField("robotID");
      DEV_ASSERT(robotIDField != nullptr, "sim_hal.Init.MissingRobotIDField");
      robotID_ = robotIDField->getSFInt32();

      //Set the motors to velocity mode
      headMotor_->setPosition(WEBOTS_INFINITY);
      liftMotor_->setPosition(WEBOTS_INFINITY);
      leftWheelMotor_->setPosition(WEBOTS_INFINITY);
      rightWheelMotor_->setPosition(WEBOTS_INFINITY);

      // Load motor array
      motors_[MOTOR_LEFT_WHEEL] = leftWheelMotor_;
      motors_[MOTOR_RIGHT_WHEEL] = rightWheelMotor_;
      motors_[MOTOR_HEAD] = headMotor_;
      motors_[MOTOR_LIFT] = liftMotor_;

      // Load position sensor array
      motorPosSensors_[MOTOR_LEFT_WHEEL] = leftWheelPosSensor_;
      motorPosSensors_[MOTOR_RIGHT_WHEEL] = rightWheelPosSensor_;
      motorPosSensors_[MOTOR_HEAD] = headPosSensor_;
      motorPosSensors_[MOTOR_LIFT] = liftPosSensor_;


      // Initialize motor positions
      for (int i=0; i < MOTOR_COUNT; ++i) {
        motorPositions_[i] = 0;
        motorPrevPositions_[i] = 0;
        motorSpeeds_[i] = 0;
        motorSpeedCoeffs_[i] = 0.5;
      }

      // Enable position measurements on head, lift, and wheel motors
      leftWheelPosSensor_->enable(ROBOT_TIME_STEP_MS);
      rightWheelPosSensor_->enable(ROBOT_TIME_STEP_MS);

      headPosSensor_->enable(ROBOT_TIME_STEP_MS);
      liftPosSensor_->enable(ROBOT_TIME_STEP_MS);

      // Set speeds to 0
      leftWheelMotor_->setVelocity(0);
      rightWheelMotor_->setVelocity(0);
      headMotor_->setVelocity(0);
      liftMotor_->setVelocity(0);

      // Get localization sensors
      gps_ = webotRobot_.getGPS("gps");
      compass_ = webotRobot_.getCompass("compass");
      gps_->enable(ROBOT_TIME_STEP_MS);
      compass_->enable(ROBOT_TIME_STEP_MS);

      // Gyro
      gyro_ = webotRobot_.getGyro("gyro");
      gyro_->enable(ROBOT_TIME_STEP_MS);

      // Accelerometer
      accel_ = webotRobot_.getAccelerometer("accel");
      accel_->enable(ROBOT_TIME_STEP_MS);

      // Proximity sensor	
      proxCenter_ = webotRobot_.getDistanceSensor("forwardProxSensor");
      if(proxCenter_ != nullptr)
      {
        proxCenter_->enable(ROBOT_TIME_STEP_MS);
      }
      
      // Cliff sensors
      cliffSensors_[HAL::CLIFF_FL] = webotRobot_.getDistanceSensor("cliffSensorFL");
      cliffSensors_[HAL::CLIFF_FR] = webotRobot_.getDistanceSensor("cliffSensorFR");
      cliffSensors_[HAL::CLIFF_BL] = webotRobot_.getDistanceSensor("cliffSensorBL");
      cliffSensors_[HAL::CLIFF_BR] = webotRobot_.getDistanceSensor("cliffSensorBR");
      cliffSensors_[HAL::CLIFF_FL]->enable(ROBOT_TIME_STEP_MS);
      cliffSensors_[HAL::CLIFF_FR]->enable(ROBOT_TIME_STEP_MS);
      cliffSensors_[HAL::CLIFF_BL]->enable(ROBOT_TIME_STEP_MS);
      cliffSensors_[HAL::CLIFF_BR]->enable(ROBOT_TIME_STEP_MS);

      // Charge contact
      chargeContact_ = webotRobot_.getConnector("ChargeContact");
      chargeContact_->enablePresence(ROBOT_TIME_STEP_MS);
      wasOnCharger_ = false;

      // Backpack button
      backpackButtonPressedField_ =  webotRobot_.getSelf()->getField("backpackButtonPressed");
      if (backpackButtonPressedField_ == nullptr) {
        AnkiError("sim_hal.Init.NoBackpackButtonPressedField", "");
        return RESULT_FAIL;
      }

      // Touch sensor
      touchSensorTouchedField_ =  webotRobot_.getSelf()->getField("touchSensorTouched");
      if (touchSensorTouchedField_ == nullptr) {
        AnkiError("sim_hal.Init.NoTouchSensorTouchedField", "");
        return RESULT_FAIL;
      }

      if (InitRadio() != RESULT_OK) {
        AnkiError("sim_hal.Init.InitRadioFailed", "");
        return RESULT_FAIL;
      }

      // Lights
      leds_[LED_BACKPACK_FRONT] = webotRobot_.getLED("backpackLED1");
      leds_[LED_BACKPACK_MIDDLE] = webotRobot_.getLED("backpackLED2");
      leds_[LED_BACKPACK_BACK] = webotRobot_.getLED("backpackLED3");

      sysLed_ = webotRobot_.getLED("backpackLED0");

      // Simulated Battery
      batteryVoltsField_ = webotRobot_.getSelf()->getField("batteryVolts");
      DEV_ASSERT(batteryVoltsField_ != nullptr, "simHAL.Init.MissingBatteryVoltsField");

      batteryChargeRateField_ = webotRobot_.getSelf()->getField("batteryChargeRate_voltsPerMin");
      DEV_ASSERT(batteryChargeRateField_ != nullptr, "simHAL.Init.MissingBatteryChargeRateField");

      batteryDischargeRateField_ = webotRobot_.getSelf()->getField("batteryDischargeRate_voltsPerMin");
      DEV_ASSERT(batteryDischargeRateField_ != nullptr, "simHAL.Init.MissingBatteryDischargeRateField");

      // Audio Input
      audioCaptureSystem_.SetCallback(std::bind(&AudioInputCallback, std::placeholders::_1, std::placeholders::_2));
      audioCaptureSystem_.Init();

      if (audioCaptureSystem_.IsValid())
      {
        audioCaptureSystem_.StartRecording();
      }
      else
      {
        PRINT_NAMED_WARNING("HAL.Init", "AudioCaptureSystem not supported on this platform. No mic data input available.");
      }

      isInitialized = true;
      return RESULT_OK;

    } // Init()

    void HAL::Stop()
    {
      
    }

    void HAL::Destroy()
    {
      gps_->disable();
      compass_->disable();

    } // Destroy()

    bool HAL::IsInitialized(void)
    {
      return isInitialized;
    }


    void HAL::GetGroundTruthPose(f32 &x, f32 &y, f32& rad)
    {
      const double* position = gps_->getValues();
      const double* northVector = compass_->getValues();

      x = position[0];
      y = position[1];

      rad = std::atan2(-northVector[1], northVector[0]);

      //PRINT("GroundTruth:  pos %f %f %f   rad %f %f %f\n", position[0], position[1], position[2],
      //      northVector[0], northVector[1], northVector[2]);


    } // GetGroundTruthPose()


    bool HAL::IsGripperEngaged() {
      return isGripperEnabled_ && con_->getPresence()==1;
    }

    void HAL::UpdateDisplay(void)
    {
      using namespace Sim::OverlayDisplay;
     /*
      PRINT("speedDes: %d, speedCur: %d, speedCtrl: %d, speedMeas: %d\n",
              GetUserCommandedDesiredVehicleSpeed(),
              GetUserCommandedCurrentVehicleSpeed(),
              GetControllerCommandedVehicleSpeed(),
              GetCurrentMeasuredVehicleSpeed());
      */

    } // HAL::UpdateDisplay()


    bool HAL::IMUReadData(HAL::IMU_DataStructure &IMUData)
    {
      const double* gyroVals = gyro_->getValues();  // rad/s
      const double* accelVals = accel_->getValues();   // m/s^2
      
      for (int i=0 ; i<3 ; i++) {
        IMUData.gyro[i]  = (f32)(gyroVals[i]);
        IMUData.accel[i] = (f32)(accelVals[i] * 1000);
      }

      // Compute estimated IMU temperature based on measured data from Victor prototype

      // Temperature dynamics approximated by:
      // dT/dt = k * (T_final - T) , with T(0) = T_initial
      // 1st order IVP. Solution:
      // T(t) = T_final - (T_final - T_initial) * exp(-k * t)
      const float T_initial = 20.f;
      const float T_final = 70.f;    // measured on Victor prototype
      const float k = .0032f;        // constant (measured), units sec^-1
      const float t = HAL::GetTimeStamp() / 1000.f; // current time in seconds

      IMUData.temperature_degC = T_final - (T_final - T_initial) * exp(-k * t);

      // Apply gyro bias based on temperature:
      if (kSimulateGyroBias) {
        // All worst case values are given in Section 1.2 of BMI160 datasheet
        const float initialBias_dps[3] = {0.f, 0.f, 0.f};     // inital zero-rate offset at startup. worst case is +/- 10 deg/sec
        const float biasChangeDueToTemp_dps_per_degC = 0.08f; // zero-rate offset change as temperature changes. worst case 0.08 deg/sec per degC
        const float biasDueToTemperature_dps = (IMUData.temperature_degC - T_initial) * biasChangeDueToTemp_dps_per_degC;

        for (int i=0 ; i<3 ; i++) {
          IMUData.gyro[i] += DEG_TO_RAD(initialBias_dps[i] + biasDueToTemperature_dps);
        }
      }

      // Return true if IMU was already read this timestamp
      static TimeStamp_t lastReadTimestamp = 0;
      bool newReading = lastReadTimestamp != HAL::GetTimeStamp();
      lastReadTimestamp = HAL::GetTimeStamp();
      return newReading;
    }

    // Returns the motor power used for calibration [-1.0, 1.0]
    float HAL::MotorGetCalibPower(MotorID motor)
    {
      float power = 0.f;
      switch (motor) {
        case MotorID::MOTOR_LIFT:
        case MotorID::MOTOR_HEAD:
          power = -0.4f;
          break;
        default:
          PRINT_NAMED_ERROR("simHAL.MotorGetCalibPower.UndefinedType", "%d", EnumToUnderlyingType(motor));
          break;
      }
      return power;
    }

    // Set the motor power in the unitless range [-1.0, 1.0]
    void HAL::MotorSetPower(MotorID motor, f32 power)
    {
      switch(motor) {
        case MotorID::MOTOR_LEFT_WHEEL:
          leftWheelMotor_->setVelocity(WheelPowerToAngSpeed(power));
          break;
        case MotorID::MOTOR_RIGHT_WHEEL:
          rightWheelMotor_->setVelocity(WheelPowerToAngSpeed(power));
          break;
        case MotorID::MOTOR_LIFT:
          liftMotor_->setVelocity(LiftPowerToAngSpeed(power));
          break;
        case MotorID::MOTOR_HEAD:
          // TODO: Assuming linear relationship, but it's not!
          headMotor_->setVelocity(HeadPowerToAngSpeed(power));
          break;
        default:
          PRINT_NAMED_ERROR("simHAL.MotorSetPower.UndefinedType", "%d", EnumToUnderlyingType(motor));
          return;
      }
    }

    // Reset the internal position of the specified motor to 0
    void HAL::MotorResetPosition(MotorID motor)
    {
      if (motor >= MotorID::MOTOR_COUNT) {
        PRINT_NAMED_ERROR("simHAL.MotorResetPosition.UndefinedType", "%d", EnumToUnderlyingType(motor));
        return;
      }

      motorPositions_[EnumToUnderlyingType(motor)] = 0;
      //motorPrevPositions_[motor] = 0;
    }

    // Returns units based on the specified motor type:
    // Wheels are in mm/s, everything else is in degrees/s.
    f32 HAL::MotorGetSpeed(MotorID motor)
    {
      switch(motor) {
        case MotorID::MOTOR_LEFT_WHEEL:
        case MotorID::MOTOR_RIGHT_WHEEL:
          // if usingTreads, fall through to just returning motorSpeeds_ since
          // it is already stored in mm/s

        case MotorID::MOTOR_LIFT:
        case MotorID::MOTOR_HEAD:
          return motorSpeeds_[EnumToUnderlyingType(motor)];

        default:
          PRINT_NAMED_ERROR("simHAL.MotorGetSpeed.UndefinedType", "%d", EnumToUnderlyingType(motor));
          break;
      }
      return 0;
    }

    // Returns units based on the specified motor type:
    // Wheels are in mm since reset, everything else is in degrees.
    f32 HAL::MotorGetPosition(MotorID motor)
    {
      switch(motor) {
        case MotorID::MOTOR_RIGHT_WHEEL:
        case MotorID::MOTOR_LEFT_WHEEL:
          // if usingTreads, fall through to just returning motorSpeeds_ since
          // it is already stored in mm

        case MotorID::MOTOR_LIFT:
        case MotorID::MOTOR_HEAD:
          return motorPositions_[EnumToUnderlyingType(motor)];

        default:
          PRINT_NAMED_ERROR("simHAL.MotorGetPosition.UndefinedType", "%d", EnumToUnderlyingType(motor));
          return 0;
      }

      return 0;
    }


    void HAL::EngageGripper()
    {
      con_->lock();
      con_->enablePresence(ROBOT_TIME_STEP_MS);
      isGripperEnabled_ = true;
#     if DEBUG_GRIPPER
      PRINT_NAMED_DEBUG("simHAL.EngageGripper.Locked", "");
#     endif
    }

    void HAL::DisengageGripper()
    {
      con_->unlock();
      con_->disablePresence();
      isGripperEnabled_ = false;
#     if DEBUG_GRIPPER
      PRINT_NAMED_DEBUG("simHAL.DisengageGripper.Unlocked", "");
#     endif

    }

    // Forward declaration
    void ActiveObjectsUpdate();

    Result HAL::Step(void)
    {

      if(webotRobot_.step(Vector::ROBOT_TIME_STEP_MS) == -1) {
        return RESULT_FAIL;
      } else {
        MotorUpdate();
        AudioInputUpdate();

        /*
        // Always display ground truth pose:
        {
          const double* position = gps_->getValues();
          const double* northVector = compass_->getValues();

          const f32 rad = std::atan2(-northVector[1], northVector[0]);

          char buffer[256];
          snprintf(buffer, 256, "Robot %d Pose: (%.1f,%.1f,%.1f), %.1fdeg@(0,0,1)",
                   robotID_,
                   M_TO_MM(position[0]), M_TO_MM(position[1]), M_TO_MM(position[2]),
                   RAD_TO_DEG(rad));

          std::string poseString(buffer);
          webotRobot_.setLabel(robotID_, poseString, 0.5, robotID_*.05, .05, 0xff0000, 0.);
        }
         */


        // Check charging status (Debug)
        if (BatteryIsOnCharger() && !wasOnCharger_) {
          PRINT_NAMED_DEBUG("simHAL.Step.OnCharger", "");
          wasOnCharger_ = true;
        } else if (!BatteryIsOnCharger() && wasOnCharger_) {
          PRINT_NAMED_DEBUG("simHAL.Step.OffCharger", "");
          wasOnCharger_ = false;
        }

        if ((tickCnt_ % batteryUpdateRate_tics_) == 0) {
          UpdateSimBatteryVolts();
        }

        ++tickCnt_;
        return RESULT_OK;
      }


    } // step()



    // Get the number of microseconds since boot
    u32 HAL::GetMicroCounter(void)
    {
      return static_cast<u32>(webotRobot_.getTime() * 1000000.0);
    }

    void HAL::MicroWait(u32 microseconds)
    {
      u32 now = GetMicroCounter();
      while ((GetMicroCounter() - now) < microseconds)
        ;
    }

    TimeStamp_t HAL::GetTimeStamp(void)
    {
      return static_cast<TimeStamp_t>(webotRobot_.getTime() * 1000.0);
      //return timeStamp_;
    }

    void HAL::SetLED(LEDId led_id, u32 color) {
      if (leds_[led_id]) {
        leds_[led_id]->set( color >> 8 ); // RGBA -> 0RGB
      } else {
        PRINT_NAMED_ERROR("simHAL.SetLED.UnhandledLED", "%d", led_id);
      }
    }

    void HAL::SetSystemLED(u32 color)
    {
      if(sysLed_ != nullptr)
      {
        sysLed_->set(color >> 8);
      }
      else
      {
        PRINT_NAMED_ERROR("simHAL.SetSystemLED.Nullptr", "");
      }
    }

    u32 HAL::GetID()
    {
      return robotID_;
    }

    ProxSensorDataRaw HAL::GetRawProxData()
    {
      ProxSensorDataRaw proxData;

      if(proxCenter_ == nullptr)
      {
        return proxData;
      }
      
      if (PowerGetMode() == POWER_MODE_ACTIVE) {
        proxData.distance_mm = static_cast<u16>( proxCenter_->getValue() );
        // Note: These fields are spoofed with simple defaults for now, but should be computed
        // to reflect the actual behavior of the sensor once we do some more testing with it.
        proxData.signalIntensity  = 25.f;
        proxData.ambientIntensity = 0.25f;
        proxData.spadCount        = 90.f;
        proxData.timestamp_ms     = HAL::GetTimeStamp();
        proxData.rangeStatus      = RangeStatus::RANGE_VALID;
      } else {
        // Calm mode values
        proxData.distance_mm      = PROX_CALM_MODE_DIST_MM;
        proxData.signalIntensity  = 0.f;
        proxData.ambientIntensity = 0.f;
        proxData.spadCount        = 200.f;
        proxData.timestamp_ms     = HAL::GetTimeStamp();
        proxData.rangeStatus      = RangeStatus::RANGE_VALID;
      }

      return proxData;
    }

    u16 HAL::GetButtonState(const ButtonID button_id)
    {
      switch(button_id) {
        case BUTTON_CAPACITIVE:
        {        
          const u16 touchSignal = 5000;
          const u16 noTouchSignal = 4700;
          if (touchSensorTouchedField_->getSFBool()) {
            return touchSignal;
          } else {
            return noTouchSignal;
          }
        }
        case BUTTON_POWER:
        {
          return backpackButtonPressedField_->getSFBool() ? 1 : 0;
        }
        default:
        {
          AnkiError( "sim_hal.GetButtonState.UnexpectedButtonType", "Button ID=%d does not have a sensible return value", button_id);
          return 0;
        }
      }

      return 0;
    }

    u16 HAL::GetRawCliffData(const CliffID cliff_id)
    {
      assert(cliff_id < HAL::CLIFF_COUNT);
      if (PowerGetMode() == POWER_MODE_ACTIVE) {
        return static_cast<u16>(cliffSensors_[cliff_id]->getValue());
      }

      return CLIFF_CALM_MODE_VAL;
    }

    bool HAL::HandleLatestMicData(SendDataFunction sendDataFunc)
    {
      // Check if our sim mic thread has delivered more audio for us to send out
      if(micDataMutex_.try_lock())
      {
        if (micData_.empty())
        {
          micDataMutex_.unlock();
          return false;
        }

        if (_numMicMsgsSent < kMaxNumMicMsgsAllowedPerSendWindow) {
          sendDataFunc(micData_.front().data(), kInterleavedSamplesPerChunk);
          ++_numMicMsgsSent;
        }

        micData_.pop_front();
        if (micData_.empty())
        {
          micDataMutex_.unlock();
          return false;
        }

        micDataMutex_.unlock();
        return true;
      }
      return false;
    }

    f32 HAL::BatteryGetVoltage()
    {
      return batteryVoltsField_->getSFFloat();
    }

    bool HAL::BatteryIsCharging()
    {
      return (chargeContact_->getPresence() == 1);
    }

    bool HAL::BatteryIsOnCharger()
    {
      // The _physical_ robot only knows that it's on the charger if
      // the charge contacts are powered, so treat this the same as
      // BatteryIsCharging()
      return HAL::BatteryIsCharging();
    }

    bool HAL::BatteryIsDisconnected()
    {
      // NOTE: This doesn't simulate syscon cutoff after 30 min
      return false;
    }

    bool HAL::BatteryIsOverheated()
    {
      // NOTE: This doesn't simulate syscon cutoff after 30 min
      return false;
    }

    u8 HAL::BatteryGetTemperature_C()
    {
      return 40;
    }

    bool HAL::BatteryIsLow()
    {
      return (BatteryGetVoltage() < 3.6f);
    }

    bool HAL::IsShutdownImminent()
    {
      // Shutdown not yet implemented in sim
      return false;
    }

    f32 HAL::ChargerGetVoltage()
    {
      if (BatteryIsOnCharger()) {
        return 5.f;
      }
      return 0.f;
    }

    extern "C" {
    void EnableIRQ() {}
    void DisableIRQ() {}
    }


    u8 HAL::GetWatchdogResetCounter()
    {
      return 0; // Simulator never watchdogs
    }

    void HAL::PrintBodyData(u32 period_tics, bool motors, bool prox, bool battery)
    {
      AnkiWarn("HAL.PrintBodyData.NotSupportedInSim", "");
    }

    void HAL::Shutdown()
    {

    } 

    void HAL::PowerSetDesiredMode(const PowerState state)
    {
      powerState_ = state;
      if (powerState_ != POWER_MODE_ACTIVE) {
        AnkiWarn("HAL.PowerSetDesiredMode.UnsupportedMode", 
                 "Only POWER_MODE_ACTIVE behavior is actually supported in sim");
      }
    }

    HAL::PowerState HAL::PowerGetDesiredMode()
    {
      return powerState_;
    }

    HAL::PowerState HAL::PowerGetMode()
    {
      return powerState_;
    }

    bool HAL::AreEncodersDisabled()
    {
      return false;
    }

    bool HAL::IsHeadEncoderInvalid()
    {
      return false;
    }

    bool HAL::IsLiftEncoderInvalid()
    {
      return false;
    }

    const uint8_t* const HAL::GetSysconVersionInfo()
    {
      static const uint8_t arr[16] = {0};
      return arr;
    }
  
  } // namespace Vector
} // namespace Anki

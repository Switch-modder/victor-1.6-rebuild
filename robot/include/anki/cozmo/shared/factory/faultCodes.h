/**
* File: faultCodes.h
*
* Author: Al Chaussee
* Date:   4/3/2018
*
* Description: Hardware and software fault codes
*              Higher codes take precedence over lower codes
*              DO NOT MODIFY
*
* Copyright: Anki, Inc. 2018
**/

#ifndef FAULT_CODES_H
#define FAULT_CODES_H

#include <inttypes.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include "platform/anki-trace/tracing.h"

namespace Anki {
namespace Vector {

namespace FaultCode {

static const char* kFaultCodeFifoName = "/run/fault_code";

// Enum of fault codes, range 800 - 999
// Higher numbers take precedence when displaying
enum : uint16_t {
  NONE = 0,

  //Use higher numbers for head self-tests
  //display precedence over (external) body tests
  DISPLAY_FAILURE       = 990,

  CAMERA_STOPPED        = 981,
  CAMERA_FAILURE        = 980,

  //WIFI                  = 970,
  WIFI_HW_FAILURE       = 970, //local wifi hw checks only

  IMU_FAILURE           = 960,

  TOF_FAILURE           = 950,

  //critical processes
  NO_CLOUD_PROCESS      = 923,
  NO_GATEWAY            = 921,
  NO_GATEWAY_CERT       = 920,
  SYSTEMD               = 919,
  NO_ROBOT_COMMS        = 917,
  NO_ROBOT_PROCESS      = 916,
  NO_ENGINE_COMMS       = 915,
  NO_ENGINE_PROCESS     = 914,
  NO_SWITCHBOARD        = 913,
  AUDIO_FAILURE         = 911,
  STOP_BOOT_ANIM_FAILED = 909,

  //Body and external errors
  NO_BODY               = 899, //no response from syscon
  SPINE_SELECT_TIMEOUT  = 898,

  RAMPOST_ERROR         = 897,
  
  //Sensor Errors
  TOUCH_SENSOR          = 895,
  TOF                   = 894,
  CLIFF_BL              = 893,
  CLIFF_BR              = 892,
  CLIFF_FL              = 891,
  CLIFF_FR              = 890,

  //Mic Errors
  MIC_BL                = 873,
  MIC_BR                = 872,
  MIC_FL                = 871,
  MIC_FR                = 870,

  //Cloud Errors
  CLOUD_READ_ESN        = 852,
  CLOUD_TOKEN_STORE     = 851,
  CLOUD_CERT            = 850,

  //Camera config errors
  NO_CAMERA_CALIB       = 840,

  // DO NOT CHANGE any of the codes in this section
  // They are hardcoded into various system level programs
  BODY_COMMS_FAILURE    = 802,
  DFU_FAILED            = 801,
  NO_ANIM_PROCESS       = 800,


  // --------- Shutdown codes ----------

  // The battery_overheated icon is displayed by faultCodeDisplay.cpp
  // when it receives this code. Power will be cut in 30s.
  SHUTDOWN_BATTERY_CRITICAL_TEMP  = 705,

  // These codes result in only vic-dasmgr be stopped
  // Also, no fault code is displayed since power to the
  // head will be cut shortly.
  SHUTDOWN_BATTERY_CRITICAL_VOLT  = 702,
  SHUTDOWN_GYRO_NOT_CALIBRATING   = 701,
  SHUTDOWN_BUTTON                 = 700,
  // ------ End of Shutdown codes ------

  COUNT = 1000
};

// Displays a fault code to the face
// Will queue fault codes until animation process is able to read
// from the fifo
static int DisplayFaultCode(uint16_t code)
{
  printf("DisplayFaultCode: %u\n", code);
  tracepoint(anki_ust, anki_fault_code, code);
  int fifo = open(FaultCode::kFaultCodeFifoName, O_WRONLY);
  if (fifo == -1) {
    printf("DisplayFaultCode: Failed to open fifo (errno %d)\n", errno);
    return -1;
  }

  char faultCode[7] = {0};
  const int numToWrite = snprintf(faultCode, sizeof(faultCode)-1, "%u\n", code);
  const ssize_t numWritten = write(fifo, faultCode, numToWrite);

  if (close(fifo) != 0) {
    printf("DisplayFaultCode: Failed to close fifo (errno %d)\n", errno);
  }

  if (numWritten != numToWrite)
  {
    printf("DisplayFaultCode: Expected to write %d bytes but only wrote %zd (errno = %d)\n",
           numToWrite,
           numWritten,
           errno);
    return -1;
  }

  return 0;
}

}
}
}

#endif

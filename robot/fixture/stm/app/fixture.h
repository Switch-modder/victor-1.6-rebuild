#ifndef FIXTURE_H
#define FIXTURE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

//-----------------------------------------------------------------------------
//                  Fixture Modes
//-----------------------------------------------------------------------------

#define FIXMODE_NONE           0   //Not assigned, or invalid
#define FIXMODE_DEBUG          1

#define FIXMODE_BODY0A         4
#define FIXMODE_BODY0          5
#define FIXMODE_BODY1          6
#define FIXMODE_BODY1_OL       7
#define FIXMODE_BODY2          8
#define FIXMODE_BODY3          9

#define FIXMODE_HEAD1          11
#define FIXMODE_HEAD1_OL       12
#define FIXMODE_HEAD2          13
#define FIXMODE_HELPER1        14

#define FIXMODE_BACKPACK1      16

#define FIXMODE_CUBE_OL        18 /*DEBUG*/
#define FIXMODE_CUBEFCC        19
#define FIXMODE_CUBE0          20 /*DEBUG*/
#define FIXMODE_CUBE1          21
#define FIXMODE_CUBE2          22

#define FIXMODE_ROBOT0         24
#define FIXMODE_ROBOT1_OL      25
#define FIXMODE_ROBOT1         26
#define FIXMODE_ROBOT2         27
#define FIXMODE_ROBOT3         28
#define FIXMODE_ROBOT3_OL      29
#define FIXMODE_ROBOT_GYM      30

#define FIXMODE_INFO           31
#define FIXMODE_PACKOUT        33
#define FIXMODE_PACKOUT_OL     34
#define FIXMODE_RECHARGE0      37
#define FIXMODE_RECHARGE1      38
#define FIXMODE_SOUND1         40
#define FIXMODE_SOUND2         41
#define FIXMODE_LOG_DL         43

#define FIXMODE_MOTOR1         46
#define FIXMODE_MOTOR2         47
#define FIXMODE_MOTOR3         48

#define FIXMODE_BLOCK1         56
#define FIXMODE_BLOCK2         57

//-----------------------------------------------------------------------------
//                  Fixture Mode extended info
//-----------------------------------------------------------------------------

#ifndef FIXTURE_EXTERNAL
#include "tests.h"
#include "time.h"

typedef struct {
  const char       *name;
  bool            (*Detect)(void); //true if test pcba/assembly is detected
  TestFunction*   (*GetTests)(void); //return ptr to NULL-terminated array of test functions
  void            (*Cleanup)(void); //called after tests (or after an exception) to de-init any resources and exit gracefully
  int               mode; //include the [redundant] mode field for runtime DDC (dynamic dummy check)
} fixmode_info_t;

//Global data
extern int                  g_fixmode; //runtime fixture mode
extern const fixmode_info_t g_fixmode_info[]; //mode information (see array init dat below)
extern const int            g_num_fixmodes; //number of modes
extern bool                 g_allowOutdated;

//Fixture prototypes
#ifdef __cplusplus
void          fixtureInit(void); //initialize fixture, board rev, pins etc.
const char*   fixtureName(void); //gets name of the current fixmode
bool          fixtureDetect(int min_delay_us = 1000); //runs detect on the current fixmode. 'min_delay_us' pads out a minimum execution time per call.
void          fixtureCleanup(void); //runs cleanup on the current fixmode
TestFunction* fixtureGetTests(void); //gets tests for the current fixmode
int           fixtureGetTestCount(void); //gets the # of test functions for the current fixmode
bool          fixtureValidateFixmodeInfo(bool print=0); //dev tool - validate const info array. print=1 -> print the array to console
uint32_t      fixtureGetSerial(void); // Get a serial number for a device in the normal 12.20 fixture.sequence format
uint32_t      fixtureReadSerial(void); // Read-only (NO MODIFY) current serial number position 12.20 format
int           fixtureReadSequence(void); //read the current sequence #. DOES NOT make any sequence changes. reporting API only.
int           fixtureSetTime(time_t time); //return 0=success
time_t        fixtureGetTime(void);
time_t        fixtureGetSetTime(void); //returns last successful fixtureSetTime() value, 0 if unknown
bool          fixtureTimeIsValid(void);
const char*   fixtureTimeStr(time_t time); //short (20char) string representation of the timestamp
#endif

//Init data for g_fixmode_info[] - keep all mode info organized in one file!
//note: FIXMODE_x must equal array index - very important, don't screw it up.
#define FIXMODE_INFO_INIT_DATA                                                                                                        \
  { "NONE"        , NULL                , NULL                        , NULL                        , FIXMODE_NONE        },  /*0*/   \
  { "DEBUG"       , TestDebugDetect     , TestDebugGetTests           , TestDebugCleanup            , FIXMODE_DEBUG       },  /*1*/   \
  { NULL          , NULL                , NULL                        , NULL                        , -1 },                           \
  { NULL          , NULL                , NULL                        , NULL                        , -1 },                           \
  { "BODY0A"      , TestBodyDetect      , TestBody0GetTests           , TestBodyCleanup             , FIXMODE_BODY0A      },  /*4*/   \
  { "BODY0"       , TestBodyDetect      , TestBody0GetTests           , TestBodyCleanup             , FIXMODE_BODY0       },  /*5*/   \
  { "BODY1"       , TestBodyDetect      , TestBody1GetTests           , TestBodyCleanup             , FIXMODE_BODY1       },  /*6*/   \
  { "BODY1-OL"    , TestBodyDetect      , TestBody1GetTests           , TestBodyCleanup             , FIXMODE_BODY1_OL    },  /*7*/   \
  { "BODY2"       , TestBodyDetect      , TestBody2GetTests           , TestBodyCleanup             , FIXMODE_BODY2       },  /*8*/   \
  { "BODY3"       , TestBodyDetect      , TestBody3GetTests           , TestBodyCleanup             , FIXMODE_BODY3       },  /*9*/   \
  { NULL          , NULL                , NULL                        , NULL                        , -1 },                           \
  { "HEAD1"       , TestHeadDetect      , TestHead1GetTests           , TestHeadCleanup             , FIXMODE_HEAD1       },  /*11*/  \
  { "HEAD1-OL"    , TestHeadDetect      , TestHead1GetTests           , TestHeadCleanup             , FIXMODE_HEAD1_OL    },  /*12*/  \
  { "HEAD2"       , TestHeadDetect      , TestHead2GetTests           , TestHeadCleanup             , FIXMODE_HEAD2       },  /*13*/  \
  { "HELPER1"     , TestHeadDetect      , TestHelper1GetTests         , TestHeadCleanup             , FIXMODE_HELPER1     },  /*14*/  \
  { NULL          , NULL                , NULL                        , NULL                        , -1 },                           \
  { "BACKPACK1"   , TestBackpackDetect  , TestBackpack1GetTests       , TestBackpackCleanup         , FIXMODE_BACKPACK1   },  /*16*/  \
  { NULL          , NULL                , NULL                        , NULL                        , -1 },                           \
  { "CUBE-OL"     , TestCubeDetect      , TestCubeOLGetTests          , TestCubeCleanup             , FIXMODE_CUBE_OL     },  /*18*/  \
  { "CUBEFCC"     , TestCubeDetect      , TestCubeFccGetTests         , TestCubeCleanup             , FIXMODE_CUBEFCC     },  /*19*/  \
  { "CUBE0"       , TestCubeDetect      , TestCube0GetTests           , TestCubeCleanup             , FIXMODE_CUBE0       },  /*20*/  \
  { "CUBE1"       , TestCubeDetect      , TestCube1GetTests           , TestCubeCleanup             , FIXMODE_CUBE1       },  /*21*/  \
  { "CUBE2"       , TestCubeDetect      , TestCube2GetTests           , TestCubeCleanup             , FIXMODE_CUBE2       },  /*22*/  \
  { NULL          , NULL                , NULL                        , NULL                        , -1 },                           \
  { "ROBOT0"      , TestRobotDetect     , TestRobot0GetTests          , TestRobotCleanup            , FIXMODE_ROBOT0      },  /*24*/  \
  { "ROBOT1-OL"   , TestRobotDetect     , TestRobot1GetTests          , TestRobotCleanup            , FIXMODE_ROBOT1_OL   },  /*25*/  \
  { "ROBOT1"      , TestRobotDetect     , TestRobot1GetTests          , TestRobotCleanup            , FIXMODE_ROBOT1      },  /*26*/  \
  { "ROBOT2"      , TestRobotDetect     , TestRobot2GetTests          , TestRobotCleanup            , FIXMODE_ROBOT2      },  /*27*/  \
  { "ROBOT3"      , TestRobotDetect     , TestRobot3GetTests          , TestRobotCleanup            , FIXMODE_ROBOT3      },  /*28*/  \
  { "ROBOT3-OL"   , TestRobotDetect     , TestRobot3GetTests          , TestRobotCleanup            , FIXMODE_ROBOT3_OL   },  /*29*/  \
  { "ROBOT-GYM"   , TestRobotDetect     , TestRobotGymGetTests        , TestRobotCleanup            , FIXMODE_ROBOT_GYM   },  /*30*/  \
  { "ROBOTINFO"   , TestRobotDetect     , TestRobotInfoGetTests       , TestRobotCleanup            , FIXMODE_INFO        },  /*31*/  \
  { NULL          , NULL                , NULL                        , NULL                        , -1 },                           \
  { "PACKOUT"     , TestRobotDetect     , TestRobotPackoutGetTests    , TestRobotCleanup            , FIXMODE_PACKOUT     },  /*33*/  \
  { "PACKOUT-OL"  , TestRobotDetect     , TestRobotPackoutGetTests    , TestRobotCleanup            , FIXMODE_PACKOUT_OL  },  /*34*/  \
  { NULL          , NULL                , NULL                        , NULL                        , -1 },                           \
  { NULL          , NULL                , NULL                        , NULL                        , -1 },                           \
  { "RECHARGE0"   , TestRobotDetect     , TestRobotRechargeGetTests   , TestRobotCleanup            , FIXMODE_RECHARGE0   },  /*37*/  \
  { "RECHARGE1"   , TestRobotDetect     , TestRobotRechargeGetTests   , TestRobotCleanup            , FIXMODE_RECHARGE1   },  /*38*/  \
  { NULL          , NULL                , NULL                        , NULL                        , -1 },                           \
  { "SOUND1"      , TestRobotDetect     , TestRobotSoundGetTests      , TestRobotCleanup            , FIXMODE_SOUND1      },  /*40*/  \
  { "SOUND2"      , TestRobotDetect     , TestRobotSoundGetTests      , TestRobotCleanup            , FIXMODE_SOUND2      },  /*41*/  \
  { NULL          , NULL                , NULL                        , NULL                        , -1 },                           \
  { "LOG-DL"      , TestRobotDetect     , TestRobotLogDownloadTests   , TestRobotCleanup            , FIXMODE_LOG_DL      },  /*43*/  \
  { NULL          , NULL                , NULL                        , NULL                        , -1 },                           \
  { NULL          , NULL                , NULL                        , NULL                        , -1 },                           \
  { "MOTOR1"      , TestMotorDetect     , TestMotor1GetTests          , TestMotorCleanup            , FIXMODE_MOTOR1      },  /*46*/  \
  { "MOTOR2"      , TestMotorDetect     , TestMotor2GetTests          , TestMotorCleanup            , FIXMODE_MOTOR2      },  /*47*/  \
  { "MOTOR3"      , TestMotorDetect     , TestMotor3GetTests          , TestMotorCleanup            , FIXMODE_MOTOR3      },  /*48*/  \
  { NULL          , NULL                , NULL                        , NULL                        , -1 },                           \
  { NULL          , NULL                , NULL                        , NULL                        , -1 },                           \
  { NULL          , NULL                , NULL                        , NULL                        , -1 },                           \
  { NULL          , NULL                , NULL                        , NULL                        , -1 },                           \
  { NULL          , NULL                , NULL                        , NULL                        , -1 },                           \
  { NULL          , NULL                , NULL                        , NULL                        , -1 },                           \
  { NULL          , NULL                , NULL                        , NULL                        , -1 },                           \
  { "BLOCK1"      , TestBlockDetect     , TestBlock1GetTests          , TestBlockCleanup            , FIXMODE_BLOCK1      },  /*56*/  \
  { "BLOCK2"      , TestBlockDetect     , TestBlock2GetTests          , TestBlockCleanup            , FIXMODE_BLOCK2      },  /*57*/  \
  { NULL          , NULL                , NULL                        , NULL                        , -1 },                           \
  { NULL          , NULL                , NULL                        , NULL                        , -1 },                           \
  { NULL          , NULL                , NULL                        , NULL                        , -1 },                           \
  { NULL          , NULL                , NULL                        , NULL                        , -1 },                           \
  { NULL          , NULL                , NULL                        , NULL                        , -1 },                           \
/*FIXMODE_INFO_ARRAY_INIT*/

#endif //ifndef SYSTEST

//-----------------------------------------------------------------------------
//                  Fixture Errors
//-----------------------------------------------------------------------------

typedef int error_t;

//RESERVED RANGES:
//001-099	Playpen
//100-199	Syscon
//200-219	Recovery / OTA
//220-299	Robot Process
//300-799	Factory fixtures
//800-999	Power on self check error code

#define IS_INTERNAL_ERROR(e) (e >= 750 && e <= 799)
#define ERROR_OK                    0

//Note: export tags, <export...>, are used by a script to export error code listing to a CSV file
//<export start>
//<export heading> ANKI VICTOR FIXTURE ERROR CODES

//<export heading> General Motor Errors
#define ERROR_MOTOR_LEFT                  310 // Problem driving left motor
//#define ERROR_MOTOR_LEFT_SPEED          311 // Problem driving left motor at full speed
//#define ERROR_MOTOR_LEFT_JAM            312 // Jamming test failed on left motor

#define ERROR_MOTOR_RIGHT                 320 // Problem driving right motor
//#define ERROR_MOTOR_RIGHT_SPEED         321 // Problem driving right motor at full speed
//#define ERROR_MOTOR_RIGHT_JAM           322 // Jamming test failed on right motor

#define ERROR_MOTOR_LIFT                  330 // Problem driving lift motor
#define ERROR_MOTOR_LIFT_RANGE            331 // Lift can't reach full range (bad encoder/mechanical blockage)
#define ERROR_MOTOR_LIFT_BACKWARD         332 // Lift is wired backward
#define ERROR_MOTOR_LIFT_NOSTOP           333 // Lift does not hit stop (bad encoder/missing lift arm)
#define ERROR_MOTOR_LIFT_SPEED            334 // Lift moving too slowly (sticky). check obstructions & gearbox
//#define ERROR_MOTOR_LIFT_JAM            334 // Jamming test failed on lift motor

#define ERROR_MOTOR_HEAD                  340 // Problem driving head motor
#define ERROR_MOTOR_HEAD_RANGE            341 // Head can't reach full range (bad encoder/mechanical blockage)
#define ERROR_MOTOR_HEAD_BACKWARD         342 // Head is wired backward
#define ERROR_MOTOR_HEAD_NOSTOP           343 // Head does not hit stop (bad encoder/missing head arm)
#define ERROR_MOTOR_HEAD_SPEED            344 // Head moving too slowly (sticky). check obstructions & gearbox
//#define ERROR_MOTOR_HEAD_SLOW_RANGE     345 // Head can't reach full range when run at low voltage
//#define ERROR_MOTOR_HEAD_JAM            346 // Jamming test failed on head motor

//<export heading> Sensor Errors
#define ERROR_SENSOR_VBAT                 350 // unable to read battery voltage. check body firmware. possible SMT problem
#define ERROR_SENSOR_CLIFF_FL             351 // cliff sensor malfunction (front left)
#define ERROR_SENSOR_CLIFF_FR             352 // cliff sensor malfunction (front right)
#define ERROR_SENSOR_CLIFF_BL             353 // cliff sensor malfunction (back left)
#define ERROR_SENSOR_CLIFF_BR             354 // cliff sensor malfunction (back right)
#define ERROR_SENSOR_TOF                  355 // distance sensor malfunction
#define ERROR_SENSOR_TOUCH                356 // touch sensor malfunction

//<export heading> Robot Errors
#define ERROR_ROBOT_TEST_SEQUENCE         370 // This test cannot run until all previous tests have passed
#define ERROR_ROBOT_PACKED_OUT            371 // test or function is locked because the robot is packed out
#define ERROR_ROBOT_MISSING_LOGFILE       372 // expected logfile not found on this robot
#define ERROR_ROBOT_INVALID_ESN           373 // invalid ESN read from the robot
#define ERROR_ROBOT_INVALID_BODY_EIN      374 // invalid EIN read from robot body pcba
#define ERROR_ROBOT_BAD_LOGFILE           375 // missing, bad or corrupt logfile

//<export heading> Testport Errors - charge contact communications (BODY ROBOT PACKOUT)
#define ERROR_TESTPORT_CMD_TIMEOUT        400 //timeout waiting for response to a command
#define ERROR_TESTPORT_RSP_MISMATCH       401 //response doesn't match command
#define ERROR_TESTPORT_RSP_MISSING_ARGS   402 //response is missing required arguments
#define ERROR_TESTPORT_RSP_BAD_ARG        403 //response arg incorrect or improperly formatted
#define ERROR_TESTPORT_CMD_FAILED         404 //returned fail code
#define ERROR_TESTPORT_RX_ERROR           405 //driver (uart) overflow or dropped chars detected

#define ERROR_SPINE_RX_ERROR              410 //driver (uart) overflow or dropped chars detected (internal error - Anki code fault)
#define ERROR_SPINE_PKT_UNSUPPORTED_TYPE  411 //received unsuppoted packet type (check firmware versions)
#define ERROR_SPINE_PKT_CHECKSUM          412 //received packet with bad checksum (bad wire? check firmware versions)
#define ERROR_SPINE_PKT_TIMEOUT           413 //timeout waiting for spine packets (check power. bad spine wire?)
#define ERROR_SPINE_CMD_TIMEOUT           414 //timeout waiting for response to spine command (check power. check firmware versions)
#define ERROR_SPINE_POWER                 415 //bad power from spine cable

//<export heading> SWD Errors (mcu programming interface)
#define ERROR_SWD_CHIPINIT                420 // failed to initialze mcu core over SWD (check power & wiring)
#define ERROR_SWD_INIT_FAIL               421 // failed to initialze mcu core over SWD (check power & wiring)
#define ERROR_SWD_READ_FAULT              422 // SWD register read failed
#define ERROR_SWD_WRITE_FAULT             423 // SWD register write failed
#define ERROR_SWD_FLASH_ERASE             424 // SWD flash erase operation failed
#define ERROR_SWD_FLASH_WRITE             425 // SWD flash write operation failed
#define ERROR_SWD_FLASH_VERIFY            426 // SWD flash verify operation failed
#define ERROR_SWD_JTAG_LOCK               427 // failed to lock jtag

//<export heading> Headprogram Errors
//RESERVED RANGE: 500-520 for helper 'headprogram' script exit codes
#define ERROR_HEADPGM                     500 // Headprogram script error (general failure)
#define ERROR_HEADPGM_MISSING_CERT        501 // no cloud certificate matching the given ESN (need new cloud cert file?)
#define ERROR_HEADPGM_BAD_CERT            502 // detect bad,corrupt cloud certificate file
#define ERROR_HEADPGM_FAC_IMG             503 // error provisioning factory partition image
#define ERROR_HEADPGM_USB_DEV_NOT_FOUND   504 // no valid DUT detected on USB - check power and wiring
#define ERROR_HEADPGM_USB_MODE_SWITCH     505 // fixture usb driver error - failed to enter otg host mode - POWER CYCLE FIXTURE
#define ERROR_HEADPGM_RANGE_END           520 // RANGE CHECK FOR HEADPGM ERRORS, do not throw this!

//<export heading> Body Errors
#define ERROR_BODY                        550 // body error (general)
//#define ERROR_BODY_BOOTLOADER           551 // Can't load bootloader onto body
//#define ERROR_BODY_OUTOFDATE            552 // Body board is running out of date firmware
#define ERROR_BODY_TREAD_ENC_LEFT         553 // left tread encoder failed self test
#define ERROR_BODY_TREAD_ENC_RIGHT        554 // right tread encoder failed self test
//#define ERROR_BODY_BACKPACK_PULL        555 // backpack pull-up incorrect
//#define ERROR_BODYCOLOR_INVALID         556 // an invalid color code was detected
#define ERROR_BODY_CANNOT_POWER_OFF       557 // robot can't turn off. check test connection and button not held. Possible damage on Body PCBA
#define ERROR_BODY_PROGRAMMED             558 // already has production fw (jtag lock prevents reprogram)
#define ERROR_BODY_NO_BOOT_MSG            559 // Could not detect production fw boot msg
#define ERROR_BODY_RANGE_END              560 // RANGE CHECK FOR BODY ERRORS, do not throw this!

//<export heading> Drop Sensor Errors
//#define ERROR_DROP_LEAKAGE              570 // Drop leakage detected
//#define ERROR_DROP_TOO_DIM              571 // Drop sensor bad LED (or reading too dim)
//#define ERROR_DROP_TOO_BRIGHT           572 // Drop sensor bad photodiode (or too much ambient light)

//<export heading> Power System Errors
#define ERROR_POWER_SHORT                 619 // Power short circuit detected
//#define ERROR_BAT_LEAKAGE               620 // Too much leakage through battery when turned off
#define ERROR_BAT_UNDERVOLT               621 // Battery voltage too low
#define ERROR_BAT_CHARGER                 622 // Battery charger not working
#define ERROR_BAT_OVERVOLT                623 // Battery voltage too high for this test (e.g. charge test)
#define ERROR_OUTPUT_VOLTAGE_LOW          624 // Fixture's output voltage is too low - check supply and wiring
#define ERROR_OUTPUT_VOLTAGE_HIGH         625 // Fixture's output voltage is too high - check supply and wiring

//<export heading> Backpack Errors
#define ERROR_BACKP_LED                   650 // Backpack LED miswired or bad LED
#define ERROR_BACKP_BTN_PRESS_TIMEOUT     651 // Timeout waiting for backpack button to be pressed
#define ERROR_BACKP_BTN_RELEASE_TIMEOUT   652 // Timeout waiting for backpack button to be released
//#define ERROR_BACKP_BTN_THRESH          653 // Button voltage detected outside digital thresholds

//<export heading> Cube Errors
#define ERROR_CUBE_CANNOT_WRITE           700 // MCU is locked
#define ERROR_CUBE_NO_COMMUNICATION       701 // MCU broken wire or locked. Already OTP'd? Bad crystal?
#define ERROR_CUBE_VERIFY_FAILED          702 // OTP is not empty or did not program correctly
#define ERROR_CUBE_LED                    703 // Detected bad LED
#define ERROR_CUBE_LED_D1                 704 // Detected bad LED [D1]
#define ERROR_CUBE_LED_D2                 705 // Detected bad LED [D2]
#define ERROR_CUBE_LED_D3                 706 // Detected bad LED [D3]
#define ERROR_CUBE_LED_D4                 707 // Detected bad LED [D4]
#define ERROR_CUBE_ACCEL                  708 // Accelerometer cannot communicate
#define ERROR_CUBE_ACCEL_PWR              709 // Accelerometer power fault
#define ERROR_CUBE_BAD_BINARY             710 // Fixture has corrupted binary (contact Anki for firmware update)
#define ERROR_CUBE_FW_MISMATCH            711 // Cube already has fw burned to OTP -- different than current version
#define ERROR_CUBE_FW_MATCH               712 // Cube already has fw burned to OTP -- same as current version
#define ERROR_CUBE_BAD_POWER              713 // Cube no-power or unstable voltage (bad wire or mcu boost reg)
//#define ERROR_CUBE_UNDERPOWER           714 // Bad power regulator
//#define ERROR_CUBE_OVERPOWER            715 // Too much power in active mode
//#define ERROR_CUBE_STANDBY              716 // Too much power in standby mode

//#define ERROR_CUBEX_SCAN_FAILED         720 // Did not detect advertising packets from the cube's radio
//#define ERROR_CUBEX_TYPE                721 // Cube type (1 2...) does not match fixture type (1 2...)
//#define ERROR_CUBEX_RADIO               722 // Bad radio/antenna

//<export heading> General Errors
#define ERROR_DEVICE_NOT_DETECTED         740 // Manual test start - DUT was not detected

//<export heading> Internal Errors - generally programming or hardware errors
#define ERROR_TIMEOUT                     750 // The operation timed out
#define ERROR_EMPTY_COMMAND               751
#define ERROR_ACK1                        752
#define ERROR_ACK2                        753
#define ERROR_UNKNOWN_MODE                754
#define ERROR_OUT_OF_RANGE                755
#define ERROR_SERIAL_EXISTS               756
#define ERROR_LOT_CODE                    757
#define ERROR_OUT_OF_SERIALS              758 // When the fixture itself runs out of 500000 serial numbers
#define ERROR_FLASH_VERIFY                759 // Flash verification failed
#define ERROR_SERIAL_INVALID              760 // valid fixture serial # {1-4095} required to generate ESN for production programming
#define ERROR_INCOMPATIBLE_FIX_REV        761 // Test is incompatible with the current fixture hardware revision
#define ERROR_INVALID_STATE               762 // Internal state is out of sync
#define ERROR_BAD_ARG                     763 // Argument errror
#define ERROR_THROW_0                     764 // Test threw exception with ERROR_OK?
#define ERROR_UNHANDLED_EXCEPTION         765 // 'safe' operation threw an uncategorized error
#define ERROR_BUFFER_TOO_SMALL            766 // provided buffer was too small
#define ERROR_INVALID_RTC                 767 // RTC clock needs to be set. Replace backup battery, if requried

//<export end>

#ifdef __cplusplus
}
#endif

#endif //FIXTURE_H

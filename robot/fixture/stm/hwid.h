#ifndef HARDWARE_ID_H
#define HARDWARE_ID_H

#include <stdint.h>

//Serialization requirements - Every board needs:
//32-bit ESN (generated by fixture GetSerial())
//16-bit hardware revision (1=DVT1, 2=DVT2, etc)
//16-bit model (1="default"/black, 2="special edition") - also cube type
  
//-----------------------------------------------------------
//        Cube
//-----------------------------------------------------------

#define CUBEID_ESN_INVALID      0

#define CUBEID_HWREV_INVALID    0 //unprogrammed/empty OTP value
#define CUBEID_HWREV_DVT1       0 //early cubes didn't have hw.rev written
#define CUBEID_HWREV_DVT2       2
#define CUBEID_HWREV_DVT3       3
#define CUBEID_HWREV_DVT4       4
#define CUBEID_HWREV_PVT        5

#define CUBEID_MODEL_INVALID    0 //unprogrammed/empty OTP value
#define CUBEID_MODEL_CUBE1      1
#define CUBEID_MODEL_CUBE2      2

typedef struct {
  uint32_t  esn;
  uint16_t  hwrev;
  uint16_t  model; //aka cube type
} cubeid_t;

//-----------------------------------------------------------
//        Body (syscon)
//-----------------------------------------------------------

#define BODYID_ESN_INVALID      0
#define BODYID_ESN_EMPTY        0xFFFFffff

#define BODYID_HWREV_EMPTY      0xffff //unprogrammed/empty value
#define BODYID_HWREV_DVT1       0xffff //early build didn't have hw.rev written
#define BODYID_HWREV_DVT2       2
#define BODYID_HWREV_DVT3       3
#define BODYID_HWREV_DVT4       4
#define BODYID_HWREV_PVT        5

#define BODYID_HWREV_IS_VALID(r)  ((r) > 1 && (r) <= BODYID_HWREV_PVT)

#define BODYID_MODEL_EMPTY      0xffff //unprogrammed/empty value
#define BODYID_MODEL_BLACK_STD  1
#define BODYID_MODEL_IS_VALID(m)  ((m) > 0 && (m) <= BODYID_MODEL_BLACK_STD)
//#define BODYID_MODEL_SE_{COLOR} 2

typedef struct {
  uint32_t  esn;
  uint16_t  hwrev;
  uint16_t  model;
} bodyid_t;

//-----------------------------------------------------------
//        Head
//-----------------------------------------------------------

#define HEADID_ESN_INVALID      0
#define HEADID_ESN_EMPTY        0xFFFFffff

#define HEADID_HWREV_EMPTY      0x0 //unprogrammed/empty value
#define HEADID_HWREV_DEBUG      0x1 //debug use and DVT1-3
#define HEADID_HWREV_DVT4       0x4
#define HEADID_HWREV_PVT        0x5
#define HEADID_HWREV_MP         0x6
#define HEADID_HWREV_WHSK_DVT1  0x7 //Whiskey (Vector 2019)
#define HEADID_HWREV_WHSK_MAX   0x19 //Whiskey (Old revisions end here)
#define HEADID_HWREV_XRAY_EVT   0x20 //XRay (Vector 2.0)

#define HEADID_HWREV_IS_VALID(r)  ( ((r) == HEADID_HWREV_XRAY_EVT) || ((r) > 0 && (r) <= HEADID_HWREV_PVT)

typedef struct {
  uint32_t  esn;
} headid_t;

#endif //HARDWARE_ID_H

/**
 * File: anki/cozmo/robot/DAS.h
 *
 * Description: DAS logging functions for vic-robot
 *              Mostly duplicated from util/logging/DAS.h so as not have to include
 *              all of Util in vic-robot
 *
 * Copyright: Anki, Inc. 2018
 *
 **/


#ifndef __robot_logging_DAS_h
#define __robot_logging_DAS_h

#include <string>
#include <stdint.h>

//
// This file must be #included by each CPP file that uses DASMSG macros, to
// ensure that macros are expanded correctly by doxygen.  If this file
// is included through an intermediate header, code will compile correctly
// but doxygen will not expand macros as expected.
//
#if !defined(__INCLUDE_LEVEL__)
#error "This header may not be included by other headers. We rely on GCC __INCLUDE_LEVEL__ to enforce this restriction."
#error "If your compiler does not define __INCLUDE_LEVEL__, please add an appropriate check for your compiler."
#endif

#if (__INCLUDE_LEVEL__ > 1)
#error "This header must be included directly from your CPP file. This header may not be included by other headers."
#error "If this header is included by another header, doxygen macros will not expand correctly."
#endif

namespace Anki {
namespace Vector {
namespace RobotInterface {
namespace DAS {  

//
// DAS v2 event fields
// https://ankiinc.atlassian.net/wiki/spaces/SAI/pages/221151429/DAS+Event+Fields+for+Victor+Robot
//
constexpr const char * SOURCE = "$source";
constexpr const char * EVENT = "$event";
constexpr const char * TS = "$ts";
constexpr const char * SEQ = "$seq";
constexpr const char * LEVEL = "$level";
constexpr const char * PROFILE = "$profile";
constexpr const char * ROBOT = "$robot";
constexpr const char * ROBOT_VERSION = "$robot_version";
constexpr const char * FEATURE_TYPE = "$feature_type";
constexpr const char * FEATURE_RUN = "$feature_run";
constexpr const char * STR1 = "$s1";
constexpr const char * STR2 = "$s2";
constexpr const char * STR3 = "$s3";
constexpr const char * STR4 = "$s4";
constexpr const char * INT1 = "$i1";
constexpr const char * INT2 = "$i2";
constexpr const char * INT3 = "$i3";
constexpr const char * INT4 = "$i4";

//
// DAS event marker
//
// This is the character chosen to precede a DAS log event on Victor.
// This is used as a hint that a log message should be parsed as an event.
// If this value changes, event readers and event writers must be updated.
//
constexpr const char EVENT_MARKER = '@';

//
// DAS field marker
//
// This is the character chosen to separate DAS log event fields on Victor.
// This character may not appear in event data.
// If this value changes, event readers and event writers must be updated.
//
constexpr const char FIELD_MARKER = '\x1F';

//
// DAS field count
// This is the number of fields used to represent a DAS log event on Victor.
// If this value changes, event readers and event writers must be updated.
//
constexpr const int FIELD_COUNT = 9;

} // end namespace DAS
} // end namespace RobotInterface
} // end namespace Vector
} // end namespace Anki

namespace Anki {
namespace Vector {
namespace RobotInterface {

//
// DAS message field
//
struct DasItem
{
  DasItem() = default;
  DasItem(const std::string & valueStr) { value = valueStr; }
  DasItem(int64_t valueInt) { value = std::to_string(valueInt); }

  inline const std::string & str() const { return value; }
  inline const char * c_str() const { return value.c_str(); }

  std::string value;
};

//
// DAS message struct
//
// Event name is required. Other fields are optional.
// Event structures should be declared with DASMSG.
// Event fields should be assigned with DASMSG_SET.
//
struct DasMsg
{
  DasMsg(const std::string & eventStr) { event = eventStr; }

  std::string event;
  DasItem s1;
  DasItem s2;
  DasItem s3;
  DasItem s4;
  DasItem i1;
  DasItem i2;
  DasItem i3;
  DasItem i4;
};

// Log an error event
void sLogError(const DasMsg & dasMessage);

// Log a warning event
void sLogWarning(const DasMsg & dasMessage);

// Log an info event
void sLogInfo(const DasMsg & dasMessage);

// Log a debug event
void sLogDebug(const DasMsg & dasMessage);

} // end namespace RobotInterface
} // end namespace Vector
} // end namespace Anki


//
// DAS message macros
//
// These macros are used to ensure that developers provide some description of each event defined.
// Note these macros are expanded two ways!  In normal compilation, they are expanded into C++
// variable declarations and logging calls.  When processed by doxygen, they are are expanded
// into syntactically invalid C++ that contains magic directives to produce readable documentation.
//
#ifndef DOXYGEN

#define DASMSG(ezRef, eventName, documentation) { Anki::Vector::RobotInterface::DasMsg __DAS_msg(eventName);
#define DASMSG_SET(dasEntry, value, comment) __DAS_msg.dasEntry = Anki::Vector::RobotInterface::DasItem(value);
#define DASMSG_SEND()         Anki::Vector::RobotInterface::sLogInfo(__DAS_msg); }
#define DASMSG_SEND_WARNING() Anki::Vector::RobotInterface::sLogWarning(__DAS_msg); }
#define DASMSG_SEND_ERROR()   Anki::Vector::RobotInterface::sLogError(__DAS_msg); }
#define DASMSG_SEND_DEBUG()   Anki::Vector::RobotInterface::sLogDebug(__DAS_msg); }

#else

/*! \defgroup dasmsg DAS Messages
*/

class DasDoxMsg() {}

#define DASMSG(ezRef, eventName, documentation)  }}}}}}}}/** \ingroup dasmsg */ \
                                            /** \brief eventName */ \
                                            /** documentation */ \
                                            class ezRef(): public DasDoxMsg() { \
                                            public:
#define DASMSG_SET(dasEntry, value, comment) /** @param dasEntry comment \n*/
#define DASMSG_SEND };
#define DASMSG_SEND_WARNING };
#define DASMSG_SEND_ERROR };
#define DASMSG_SEND_DEBUG };
#endif

#endif // __robot_logging_DAS_h

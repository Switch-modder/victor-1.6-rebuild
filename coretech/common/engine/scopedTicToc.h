/**
 * File: scopedTicToc.h
 *
 * Author: Andrew Stein
 * Date:   08/02/2018
 *
 * Description: Lightweight scoped tic/toc timing mechanism.
 *              A timer is started (tic) upon instantiation and ends (toc) when the timer
 *                goes out of scope.
 *              Methods are defined in the header to support easy inlining.
 *              The duration is logged as an INFO message in the specified channel.
 *              Can be compiled out using ANKI_ALLOW_SCOPED_TICTOC=0 (e.g. in SHIPPING)
 *              Can also be enabled/disabled programmatically using static Enable() call.
 *              For more advanced tracking of average timings and periodic logging, and DAS support
 *                see also the Profiler class, which also offers a form of scoped TicToc timing.
 *
 * Copyright: Anki, Inc. 2018
 **/

#include "util/logging/logging.h"
#include <chrono>

// Can be overridden from the command line, otherwise default to on if DEV_CHEATS are enabled
#ifndef ANKI_ALLOW_SCOPED_TICTOC
#  if ANKI_DEV_CHEATS
#    define ANKI_ALLOW_SCOPED_TICTOC 1
#  else
#    define ANKI_ALLOW_SCOPED_TICTOC 0
#  endif
#endif

namespace Anki {
  
class ScopedTicToc
{
public:
  using Resolution = std::chrono::milliseconds;
  using ClockType  = std::chrono::high_resolution_clock;
  static_assert(ClockType::is_steady, "ClockType should be steady");
  
  ScopedTicToc(const char* name, const char* channel = "ScopedTicToc")
# if ANKI_ALLOW_SCOPED_TICTOC
  : _name(name)
  , _channel(channel)
  , _start(ClockType::now())
# endif
  {
    
  }
  
  ~ScopedTicToc()
  {
    if(ANKI_ALLOW_SCOPED_TICTOC && _enabled)
    {
      const auto end = ClockType::now();
      const auto duration = std::chrono::duration_cast<Resolution>(end - _start);
      PRINT_CH_INFO(_channel, _name, "%dms", (int)duration.count());
    }
  }
  
  static void Enable(const bool enable) { _enabled = enable; }
  
private:
  
  static bool _enabled;
  
  const char* _name;
  const char* _channel;
  std::chrono::time_point<ClockType> _start;
};

} // namespace Anki

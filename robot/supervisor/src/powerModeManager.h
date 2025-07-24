/**
 * File: powerModeManager.h
 *
 * Author: Kevin Yoon
 * Created: 5/31/2018
 *
 * Description: Manages transitions between syscon power modes
 *
 * Copyright: Anki, Inc. 2018
 *
 **/


#ifndef COZMO_POWER_MODE_MANAGER_H_
#define COZMO_POWER_MODE_MANAGER_H_

#include "coretech/common/shared/types.h"

namespace Anki {
  namespace Vector {
    namespace PowerModeManager {
      
      void EnableActiveMode(bool enable);
      bool IsActiveModeEnabled();

      void Update();
      
    } // namespace PowerModeManager
  } // namespace Vector
} // namespace Anki

#endif // COZMO_POWER_MODE_MANAGER_H_

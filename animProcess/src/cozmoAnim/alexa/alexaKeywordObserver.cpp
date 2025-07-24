/**
 * File: alexaKeywordObserver.h
 *
 * Author: ross
 * Created: Oct 20 2018
 *
 * Description: An implementation of the KeyWordObserverInterface that uses our special AlexaClient
 *              instead of the SDK's client
 *
 * Copyright: Anki, Inc. 2018
 *
 */

// Since this file uses the sdk, here's the SDK license
/*
 * Copyright 2017-2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *     http://aws.amazon.com/apache2.0/
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include "cozmoAnim/alexa/alexaKeywordObserver.h"
#include "cozmoAnim/alexa/alexaClient.h"
#include "util/logging/logging.h"

namespace Anki {
namespace Vector {
  
using namespace alexaClientSDK;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
AlexaKeywordObserver::AlexaKeywordObserver( std::shared_ptr<AlexaClient> client,
                                            capabilityAgents::aip::AudioProvider audioProvider,
                                            std::shared_ptr<esp::ESPDataProviderInterface> espProvider )
  : _client{ client }
  , _audioProvider{ audioProvider }
  , _espProvider{ espProvider }
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void AlexaKeywordObserver::onKeyWordDetected( std::shared_ptr<avsCommon::avs::AudioInputStream> stream,
                                              std::string keyword,
                                              avsCommon::avs::AudioInputStream::Index beginIndex,
                                              avsCommon::avs::AudioInputStream::Index endIndex,
                                              std::shared_ptr<const std::vector<char>> KWDMetadata )
{
  if( (endIndex != avsCommon::sdkInterfaces::KeyWordObserverInterface::UNSPECIFIED_INDEX)
      && (beginIndex == avsCommon::sdkInterfaces::KeyWordObserverInterface::UNSPECIFIED_INDEX) )
  {
    if( _client ) {
      _client->NotifyOfTapToTalk( _audioProvider, endIndex );
    }
  } else if ( (endIndex != avsCommon::sdkInterfaces::KeyWordObserverInterface::UNSPECIFIED_INDEX)
              && (beginIndex != avsCommon::sdkInterfaces::KeyWordObserverInterface::UNSPECIFIED_INDEX) )
  {
    auto espData = capabilityAgents::aip::ESPData::getEmptyESPData();
    if( _espProvider ) {
      espData = _espProvider->getESPData();
    }

    if( _client ) {
      // TODO(ACSDK-1976): We need to take into consideration the keyword duration.
      auto startOfSpeechTimestamp = std::chrono::steady_clock::now();
      _client->NotifyOfWakeWord( _audioProvider, beginIndex, endIndex, keyword, startOfSpeechTimestamp, espData, KWDMetadata );
    }
  }
}

}  // namespace Vector
}  // namespace Anki

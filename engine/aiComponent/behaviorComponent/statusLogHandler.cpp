/**
 * File: statusLogHandler.cpp
 *
 * Author: ross
 * Created: 2018-05-07
 *
 * Description: A handler for status messages and a store for recent statuses
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#include "engine/aiComponent/behaviorComponent/statusLogHandler.h"
#include "coretech/common/engine/utils/timer.h"
#include "engine/cozmoContext.h"
#include "engine/events/ankiEvent.h"
#include "engine/externalInterface/externalMessageRouter.h"
#include "engine/externalInterface/gatewayInterface.h"
#include "proto/external_interface/messages.pb.h"
#include "util/signals/simpleSignal.hpp"

namespace Anki {
namespace Vector {

namespace {
  constexpr unsigned int kStatusHistoryLength = 100;
}

StatusLogHandler::StatusLogHandler( const CozmoContext* context )
  : _context(context)
{
  _statusHistory.reset( new StatusHistory{kStatusHistoryLength} );
  
  auto* gi = _context->GetGatewayInterface();
  if( gi != nullptr ) {
    auto handleEvent = [this](const AnkiEvent<external_interface::GatewayWrapper>& msg) {
      if( msg.GetData().GetTag() == external_interface::GatewayWrapperTag::kEvent ) {
        const auto& event = msg.GetData().event();
        if( event.GetTag() == external_interface::EventTag::kTimeStampedStatus ) {
          SaveStatusHistory( event.time_stamped_status() );
        }
      }
    };
    // this is a good example of where it would be nice to subscribe to a leaf "status" instead of a branch "event"
    _signalHandles.push_back( gi->Subscribe(external_interface::GatewayWrapperTag::kEvent, handleEvent) );
    
    // handle request
    auto handleRequest = [this](const AnkiEvent<external_interface::GatewayWrapper>& msg) {
      if( msg.GetData().GetTag() == external_interface::GatewayWrapperTag::kRobotHistoryRequest ) {
        SendStatusHistory();
      }
    };
    _signalHandles.push_back( gi->Subscribe(external_interface::GatewayWrapperTag::kRobotHistoryRequest, handleRequest) );
  }
  
  
}

void StatusLogHandler::SetFeature( const std::string& featureName, const std::string& source )
{
  // send to app
  auto* gi = _context->GetGatewayInterface();
  if( gi != nullptr ) {
    auto* status = new external_interface::FeatureStatus{ featureName, source };
    gi->Broadcast( ExternalMessageRouter::Wrap(status) );
  }
}
  
void StatusLogHandler::SaveStatusHistory( const external_interface::TimeStampedStatus& status )
{
  _statusHistory->push_back(status);
}
  
void StatusLogHandler::SendStatusHistory()
{
  auto* gi = _context->GetGatewayInterface();
  if( gi != nullptr ) {
    auto* response = new external_interface::RobotHistoryResponse;
    response->mutable_messages()->Reserve( (int)_statusHistory->size() );
    for( size_t i=0; i<_statusHistory->size(); ++i ) {
      auto* status = response->mutable_messages()->Add();
      *status = (*_statusHistory)[i];
    }
    gi->Broadcast( ExternalMessageRouter::WrapResponse(response) );
  }
}

}
}

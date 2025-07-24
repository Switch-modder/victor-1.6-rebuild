//
//  RequestsDelegate.h
//  mac-client
//
//  Created by Paul Aluri on 5/10/18.
//  Copyright © 2018 Paul Aluri. All rights reserved.
//

#ifndef RequestsDelegate_h
#define RequestsDelegate_h

typedef NS_ENUM(NSUInteger, RequestId) {
  kUnknown,
  kStatus,
  kWifiScan,
  kWifiConnect,
  kWifiIp,
  kWifiAp,
  kOta,
  kOtaStart,
  kOtaCancel,
  kOtaProgress
};

@protocol RequestDelegate

-(void) handleResponse:(RequestId)requestId message:(Anki::Vector::ExternalComms::RtsConnection_2)msg;
-(void) handleResponse_3:(RequestId)requestId message:(Anki::Vector::ExternalComms::RtsConnection_3)msg;
-(void) handleResponse_4:(RequestId)requestId message:(Anki::Vector::ExternalComms::RtsConnection_4)msg;
-(void) handleResponse_5:(RequestId)requestId message:(Anki::Vector::ExternalComms::RtsConnection_5)msg;

@end

#endif /* RequestsDelegate_h */

//
//  BleClient.h
//  mac-client
//
//  Created by Paul Aluri on 2/28/18.
//  Copyright © 2018 Paul Aluri. All rights reserved.
//

#ifndef BleClient_h
#define BleClient_h

#include <CoreBluetooth/CoreBluetooth.h>
#include <stdlib.h>
#include "bleMessageProtocol.h"
#include "messageExternalComms.h"
#include "RequestsDelegate.h"
#include <sodium.h>
#include <arpa/inet.h>

enum RtsState {
  Raw         = 0,
  Clad        = 1,
  CladSecure  = 2,
};

enum WiFiAuth : uint8_t {
  AUTH_NONE_OPEN       = 0,
  AUTH_NONE_WEP        = 1,
  AUTH_NONE_WEP_SHARED = 2,
  AUTH_IEEE8021X       = 3,
  AUTH_WPA_PSK         = 4,
  AUTH_WPA_EAP         = 5,
  AUTH_WPA2_PSK        = 6,
  AUTH_WPA2_EAP        = 7
};

@interface BleCentral : NSObject <CBCentralManagerDelegate, CBPeripheralDelegate> {
  NSString* _localName;
  
  CBCentralManager* _centralManager;
  CBUUID* _victorService;
  CBUUID* _readUuid;
  CBUUID* _writeUuid;
  CBUUID* _readSecureUuid;
  CBUUID* _writeSecureUuid;
  
  CBPeripheral* _peripheral;
  
  NSMutableDictionary* _characteristics;
  
  std::unique_ptr<Anki::Switchboard::BleMessageProtocol> _bleMessageProtocol;
  
  uint8_t _publicKey [crypto_kx_PUBLICKEYBYTES];
  uint8_t _secretKey [crypto_kx_SECRETKEYBYTES];
  uint8_t _encryptKey [crypto_kx_SESSIONKEYBYTES];
  uint8_t _decryptKey [crypto_kx_SESSIONKEYBYTES];
  uint8_t _remotePublicKey [crypto_kx_PUBLICKEYBYTES];
  uint8_t _nonceIn [crypto_aead_xchacha20poly1305_ietf_NPUBBYTES];
  uint8_t _nonceOut [crypto_aead_xchacha20poly1305_ietf_NPUBBYTES];
  
  enum RtsState _rtsState;
  bool _reconnection;
  
  NSMutableDictionary* _victorsDiscovered;
  bool _connecting;
  
  NSString* _filter;
  
  NSMutableDictionary* _wifiAuth;
  NSMutableSet* _wifiHidden;
  dispatch_queue_t _commandQueue;
  dispatch_queue_t _requestQueue;
  
  std::string _currentCommand;
  bool _readyForNextCommand;
  
  uint8_t _otaStatusCode;
  uint64_t _otaProgress;
  uint64_t _otaExpected;
  
  bool _verbose;
  bool _download;
  int _commVersion;
  bool _interactive;
  
  NSArray* _colorArray;
  
  // file download
  uint32_t _currentFileId;
  std::vector<uint8_t> _currentFileBuffer;
  NSString* _downloadFilePath;

  bool _isPairing;
  bool _hasVersion;
  int _inputVersion;
  
  bool _hasStartedPrompt;
  bool _hasAuthed;
  bool _hasOwner;
  
  NSString* _sessionName;
  NSString* _sessionIp;
  NSString* _sessionEsn;
  NSString* _sessionClientAppToken;
  
  Anki::Vector::ExternalComms::RtsConnRequest _currentConnRequest;
}

@property (strong, nonatomic) id delegate;
@property (strong, nonatomic) id syncdelegate;

- (std::string)hexStr:(char*)data length:(int)len;
- (std::string)asciiStr:(char*)data length:(int)size;
- (uint8_t)nibbleToNumber:(uint8_t)nibble;

// Versioned handlers
- (void) handleSecureVersion3: (Anki::Vector::ExternalComms::ExternalComms)extComms;
- (void) handleRequest_3:(Anki::Vector::ExternalComms::RtsConnection_3)msg;
- (void) handleSecureVersion4: (Anki::Vector::ExternalComms::ExternalComms)extComms;
- (void) handleRequest_4:(Anki::Vector::ExternalComms::RtsConnection_4)msg;
- (void) handleSecureVersion5: (Anki::Vector::ExternalComms::ExternalComms)extComms;
- (void) handleRequest_5:(Anki::Vector::ExternalComms::RtsConnection_5)msg;

- (void) devDownloadOta;
- (void) handleSend:(const void*)bytes length:(int)n;
- (void) handleReceive:(const void*)bytes length:(int)n;
- (void) handleReceiveSecure:(const void*)bytes length:(int)n;
- (void) printHelp;
- (void) showProgress: (float)current expected:(float)expected;
- (std::string) getSessionToken;
- (void) startPrompt;
- (void) handleRequest:(Anki::Vector::ExternalComms::RtsConnection_2)msg;

- (void) SendSshPublicKey:(std::string)filename;

- (void) async_StatusRequest;
- (void) async_WifiScanRequest;
- (void) async_WifiConnectRequest:(std::string)ssid password:(std::string)pw hidden:(bool)hid auth:(uint8_t)authType;
- (void) async_WifiIpRequest;
- (void) async_WifiApRequest:(bool)enabled;
- (void) async_otaStart:(std::string)url;
- (void) async_otaCancel;
- (void) async_otaProgress;

- (void) HandleReceiveHandshake:(const void*)bytes length:(int)n;
- (void) HandleReceivePublicKey:(const Anki::Vector::ExternalComms::RtsConnRequest&)msg;
- (void) HandleReceiveNonce:(const Anki::Vector::ExternalComms::RtsNonceMessage&)msg;
- (void) HandleChallengeMessage:(const Anki::Vector::ExternalComms::RtsChallengeMessage&)msg;
- (void) HandleChallengeSuccessMessage:(const Anki::Vector::ExternalComms::RtsChallengeSuccessMessage&)msg;
- (void) HandleWifiScanResponse:(const Anki::Vector::ExternalComms::RtsWifiScanResponse&)msg;
- (void) HandleWifiScanResponse_2:(const Anki::Vector::ExternalComms::RtsWifiScanResponse_2&)msg;
- (void) HandleWifiScanResponse_3:(const Anki::Vector::ExternalComms::RtsWifiScanResponse_3&)msg;
- (void) HandleReceiveAccessPointResponse:(const Anki::Vector::ExternalComms::RtsWifiAccessPointResponse&)msg;

- (void) send:(const void*)bytes length:(int)n;
- (void) sendSecure:(const void*)bytes length:(int)n;

- (void) StartScanning;
- (void) StartScanning:(NSString*)nameFilter;
- (void) StopScanning;
- (void) interrupt;

- (std::vector<std::string>) GetWordsFromLine: (std::string)line;

- (void) printSuccess:(const char*) txt;

// for reconnection
- (bool) HasSavedPublicKey;
- (bool) HasSavedSession: (NSString*)key;
- (bool) HasSessionForName: (NSString*)key;
- (NSDictionary*) GetSessionForName: (NSString*)key;
- (void) SaveName: (NSString*)key;
- (NSData*) GetPublicKey;
- (NSData*) GetPrivateKey;
- (NSArray*) GetSession: (NSString*)key;
- (void)resetDefaults;
- (void)setVerbose:(bool)enabled;
- (void)setDownload:(bool)enabled;
- (void)setHasVersion:(bool)has version:(int)v;

@end

class Clad {
public:
  template<typename T, typename... Args>
  static void SendRtsMessage(BleCentral* central, int commVersion, Args&&... args) {
    Anki::Vector::ExternalComms::ExternalComms msg;
    
    switch(commVersion) {
      case 1:
        msg = Anki::Vector::ExternalComms::ExternalComms(Anki::Vector::ExternalComms::RtsConnection_1(T(std::forward<Args>(args)...)));
        break;
      case 2:
        msg = Anki::Vector::ExternalComms::ExternalComms(Anki::Vector::ExternalComms::RtsConnection(Anki::Vector::ExternalComms::RtsConnection_2(T(std::forward<Args>(args)...))));
        break;
      case 3:
        msg = Anki::Vector::ExternalComms::ExternalComms(Anki::Vector::ExternalComms::RtsConnection(Anki::Vector::ExternalComms::RtsConnection_3(T(std::forward<Args>(args)...))));
        break;
      case 4:
        msg = Anki::Vector::ExternalComms::ExternalComms(Anki::Vector::ExternalComms::RtsConnection(Anki::Vector::ExternalComms::RtsConnection_4(T(std::forward<Args>(args)...))));
        break;
      case 5:
        msg = Anki::Vector::ExternalComms::ExternalComms(Anki::Vector::ExternalComms::RtsConnection(Anki::Vector::ExternalComms::RtsConnection_5(T(std::forward<Args>(args)...))));
        break;
      default:
        NSLog(@"The mac client is trying to speak a version we do not know about.");
        break;
    }
    std::vector<uint8_t> messageData(msg.Size());
    const size_t packedSize = msg.Pack(messageData.data(), msg.Size());
    [central send:messageData.data() length:(int)packedSize];
  }
  
  template<typename T, typename... Args>
  static void SendRtsMessage_2(BleCentral* central, int commVersion, Args&&... args) {
    Anki::Vector::ExternalComms::ExternalComms msg = Anki::Vector::ExternalComms::ExternalComms(Anki::Vector::ExternalComms::RtsConnection(Anki::Vector::ExternalComms::RtsConnection_2(T(std::forward<Args>(args)...))));

    std::vector<uint8_t> messageData(msg.Size());
    const size_t packedSize = msg.Pack(messageData.data(), msg.Size());
    [central send:messageData.data() length:(int)packedSize];
  }
  
  template<typename T, typename... Args>
  static void SendRtsMessage_3(BleCentral* central, int commVersion, Args&&... args) {
    Anki::Vector::ExternalComms::ExternalComms msg = Anki::Vector::ExternalComms::ExternalComms(Anki::Vector::ExternalComms::RtsConnection(Anki::Vector::ExternalComms::RtsConnection_3(T(std::forward<Args>(args)...))));
    
    std::vector<uint8_t> messageData(msg.Size());
    const size_t packedSize = msg.Pack(messageData.data(), msg.Size());
    [central send:messageData.data() length:(int)packedSize];
  }
  
  template<typename T, typename... Args>
  static void SendRtsMessage_4(BleCentral* central, int commVersion, Args&&... args) {
    Anki::Vector::ExternalComms::ExternalComms msg = Anki::Vector::ExternalComms::ExternalComms(Anki::Vector::ExternalComms::RtsConnection(Anki::Vector::ExternalComms::RtsConnection_4(T(std::forward<Args>(args)...))));
    
    std::vector<uint8_t> messageData(msg.Size());
    const size_t packedSize = msg.Pack(messageData.data(), msg.Size());
    [central send:messageData.data() length:(int)packedSize];
  }
  
  template<typename T, typename... Args>
  static void SendRtsMessage_5(BleCentral* central, int commVersion, Args&&... args) {
    Anki::Vector::ExternalComms::ExternalComms msg = Anki::Vector::ExternalComms::ExternalComms(Anki::Vector::ExternalComms::RtsConnection(Anki::Vector::ExternalComms::RtsConnection_5(T(std::forward<Args>(args)...))));
    
    std::vector<uint8_t> messageData(msg.Size());
    const size_t packedSize = msg.Pack(messageData.data(), msg.Size());
    [central send:messageData.data() length:(int)packedSize];
  }
};

#endif /* BleClient_h */

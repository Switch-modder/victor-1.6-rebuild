# Switchboard BLE API (RTS V5) #

**Author:** Paul Aluri

Once connected/paired securely over BLE, clients can send and receive messages to/from the robot.

## Status ##

## `RtsStatusRequest` ##
`Params: none`

### **Response** ###
```
RtsStatusResponse_5
  string      wifiSsidHex,
  uint_8      wifiState,
  bool        accessPoint,
  uint_8      bleState,
  uint_8      batteryState,
  string      version,
  string      esn,
  bool        otaInProgress,
  bool        hasOwner,
  bool        isCloudAuthed,
```

`bleState` and `batteryState` are currently unusued.

`wifiSsidHex` is ascii hex string of network if connected.

`wifiState` is from `WiFiConnState` enum. `accessPoint` is whether robot is in access point mode. `version` is the build string version. `otaInProgress` is whether the robot is currently updating itself. `esn` is the robot's serial number. `hasOwner` indicates whether the robot has a Anki account JWT token. `isCloudAuthed` reports whether the connection has been authenticated if the robot has a cloud owner.

# #
## WiFi Connections ##

## `RtsWifiScanRequest` ##
`Params: none`

`Notes: `
Before connecting to a wifi network, you should always request a scan.

### **Response** ###
```
RtsWifiScanResponse_3
  uint_8              statusCode,
  RtsWifiScanResult_3 scanResult[uint_8]
```
`statusCode` `0` means success, otherwise error is from this enum:

```
enum WifiScanErrorCode : uint8_t {
    SUCCESS                   = 0,
    ERROR_GETTING_PROXY       = 100,
    ERROR_SCANNING            = 101,
    FAILED_SCANNING           = 102,
    ERROR_GETTING_MANAGER     = 103,
    ERROR_GETTING_SERVICES    = 104,
    FAILED_GETTING_SERVICES   = 105,
}
```
```
RtsWifiScanResult_3
  uint_8      authType,
  uint_8      signalStrength,
  string      wifiSsidHex,
  bool        hidden,
  bool        provisioned,
```

`authType` is from the `WiFiAuth` enum, signalStrength is a value `(0,3)`. `provisioned` is a bool indicating whether the wifi network has a SSID/password configuration on the robot.

```
enum WiFiAuth : uint8_t {
      AUTH_NONE_OPEN       = 0,
      AUTH_NONE_WEP        = 1,
      AUTH_NONE_WEP_SHARED = 2,
      AUTH_IEEE8021X       = 3,
      AUTH_WPA_PSK         = 4,
      AUTH_WPA_EAP         = 5,
      AUTH_WPA2_PSK        = 6,
      AUTH_WPA2_EAP        = 7
}
```

## `RtsWifiConnectRequest` ##
`Params:`
```
string wifiSsidHex // ascii hex representation
string password    // plaintext string
uint_8 timeout     // max seconds to wait for response
uint_8 authType    // from `WiFiAuth` enum
bool   hidden      // whether SSID is hidden
```
### **Response** ###
```
RtsWifiConnectResponse
  string wifiSsidHex,
  uint_8 wifiState
```

`wifiSsidHex` is the name of the network the robot tried to connect to. `wifiState` is from the `WiFiConnState` enum.

```
enum WiFiConnState : uint8_t {
  UNKNOWN       = 0,
  ONLINE        = 1,
  CONNECTED     = 2,
  DISCONNECTED  = 3,
}
```

## `RtsWifiIpRequest` ##
`Params: None`

### **Response** ###
```
RtsWifiIpResponse
  uint_8      hasIpV4,
  uint_8      hasIpV6,
  uint_8      ipV4[4],
  uint_8      ipV6[16]
```

`hasIpV4` is `0` _iff_ robot has no Ipv4. `hasIpV6` is `0` _iff_ robot has no Ipv6.

## `RtsWifiAccessPointRequest` ##
`Params:`
```
bool enable // toggle on or off
```

### **Response** ###
```
RtsWifiAccessPointResponse
  bool        enabled,  // whether AP is on
  string      ssid,     // plaintext SSID of robot's AP
  string      password, // plaintext pw of robot's AP
```

## `RtsWifiForgetRequest` ##
`Params:`
```
bool        deleteAll,    // whether to forget all networks
string      wifiSsidHex,  // ssid to forget if deleteAll is false
```

### **Response** ###
```
RtsWifiForgetResponse
bool        didDelete,
string      wifiSsidHex   // ssid matching request
```

## `RtsCloudSessionRequest_2` ##
`Params:`
```
string[uint_16]      sessionToken,   // Anki account session token
string               clientName,     // client name like "Paul's iPhone"
string               appId,          // an app/client guid
```

### **Response** ###
```
RtsCloudSessionResponse
bool                  success,
RtsCloudStatus        statusCode,
string[uint_16]       clientTokenGuid,
```

## `RtsSdkProxyRequest` ##
`Params:`
```
string            clientGuid,   // a valid app client guid returned by the RtsCloudSessionResponse message
string            messageId,    // user-defined message id which will be included in the response
string            urlPath,      // sdk URL path (e.g., "v1/protocol_version")
string[uint_16]   json,         // json payload
```

### **Response** ###
```
RtsSdkProxyResponse
string            messageId,      // the message id which matches up with the id provided in the request
uint_16           statusCode,     // "HTTP" status code from SDK
string            responseType,   // "HTTP" response type (e.g., "application/text")
string[uint_16]   responseBody,   // the payload (typically JSON) of the response
```

# #
## OTA (Over-the-Air Updates) ##
## `RtsOtaUpdateRequest` ##
`Params:`
```
string url  // URL pointing to valid OTA
```

### **Response** ###
```
RtsOtaUpdateResponse
```

## `RtsAppConnectionIdRequest` ##
`Params:`
```
string[uint_16]       connectionId,
```

### **Response** ###
```
RtsAppConnectionIdResponse
```

`connectionId` is some id generated by client and given to Vector to use in DAS logs.


**SEE `Events` SECTION**

# #
## Misc. ##

## `RtsResponse` ##
    RtsResponseCode       code,
    string[uint_16]       responseMessage,

`RtsResponse` is a generic response to potentially any Rts request.

### `RtsResponseCode` ###
```
enum uint_16 RtsResponseCode {
    NotCloudAuthorized,
}
```

## `RtsLogRequest` ##
`Params:`
```
uint_8 mode,            // unused
string filter[uint_16]  // unused
```

### **Response** ###
```
RtsLogResponse
uint_8      exitCode,
uint_32     fileId
```

`exitCode` is status code of log collector script. `fileId` is value that should match following `RtsFileDownload` event.

# #
## Events ##

## `RtsCancelPairing` ##
`Params: None`

`Notes:` Robot is terminating pairing session prematurely.

## `RtsFileDownload` ##
`Params:`
```
  uint_8      status,             // = 0
  uint_32     fileId,             // unique file descriptor
  uint_32     packetNumber,       // current packet number
  uint_32     packetTotal,        // total number of packets
  uint_8      fileChunk[uint_16]  // payload
```

## `RtsOtaUpdateResponse` ##
`Params:`
```
  uint_8      status,
  uint_64     current,    // number of bytes downloaded/written
  uint_64     expected,   // total number of bytes
```

`status` is from `OtaStatusCode` enum, or if it is `> 200`, it is from the actual exit code of `update-engine.py`.

```
enum OtaStatusCode {
  UNKNOWN     = 1,
  IN_PROGRESS = 2,
  COMPLETED   = 3,
  REBOOTING   = 4,
  ERROR       = 5,
}
```



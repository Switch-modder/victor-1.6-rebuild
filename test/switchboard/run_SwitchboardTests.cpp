#include "test.h"

#include <string>
#include <vector>
#include <sodium.h>

#include "anki-wifi/wifi.h"
#include "anki-wifi/exec_command.h"
#include "switchboardd/christen.h"
#include "switchboardd/bleMessageProtocol.h"
#include "switchboardd/keyExchange.h"
#include "switchboardd/rtsComms.h"
#include "switchboardd/savedSessionManager.h"
#include "ev++.h"
#include "test_INetworkStreamV2.h"
#include "test_INetworkStreamV3.h"
#include "test_INetworkStreamV4.h"
#include "test_INetworkStreamV5.h"

struct TestData {
  bool(*method)();
  const char* errorMessage;
};

bool TEST(bool(*f)(), std::string msg, int num) {
  printf("================================\n");
  printf("Test [%d]\n", num);
  printf("================================\n");
  sTestPassed = true;
  f();
  if(!sTestPassed) {
    printf("Test (%d) failed: %s\n", num, msg.c_str());
  }

  return sTestPassed;
}

using namespace Anki::Switchboard;

bool Test_ExecCommand() {
  // some non-existant folder, ExecCommand should return 1
  bool testInvalidPath = Anki::ExecCommand({ "ls", "dflkjalsdkfjlsd/asdkf" }) == 1;
  // ExecCommand should return 0
  bool testValidPath = Anki::ExecCommand({ "ls" }) == 0;

  ASSERT(testInvalidPath && testValidPath, "Unable to Exec ls command.");

  return true;
}

bool Test_BleMessagingProtocol() {
  const size_t MAX_SIZE = 20;
  for(int iter = 0; iter < 200; iter++) {
    Anki::Switchboard::BleMessageProtocol* msgProtocol = new Anki::Switchboard::BleMessageProtocol(MAX_SIZE);
    
    const int kMsgSize = iter;
    
    uint8_t* buffer = (uint8_t*)malloc(kMsgSize);
    for(int i = 0; i < kMsgSize; i++) {
      buffer[i] = i;
    }
    
    msgProtocol->OnReceiveMessageEvent().SubscribeForever([](uint8_t* buffer, size_t size) {
      bool success = true;
      for(int i = 0; i < size; i++) {
        if(buffer[i] != (uint8_t)i) {
          success = false;
          break;
        }
      }

      ASSERT(success, "Mismatch in BleMessageProtocol transmission.");
    });
    msgProtocol->OnSendRawBufferEvent().SubscribeForever([msgProtocol](uint8_t* buffer, size_t size) {
      msgProtocol->ReceiveRawBuffer(buffer, size);
    });
    
    msgProtocol->SendMessage(buffer, kMsgSize);
    msgProtocol->SendMessage(buffer, kMsgSize);
    
    free(buffer);
    delete msgProtocol;
  }

  return true;
}

bool Test_KeyExchange() {
  const uint8_t PIN_DIGITS = 6;
  KeyExchange* keyEx = new KeyExchange(PIN_DIGITS);
  
  // generate keys and ensure publickey is returned
  uint8_t* publicKey = keyEx->GenerateKeys();
  bool publicKeyReturned = memcmp(publicKey, keyEx->GetPublicKey(), crypto_kx_PUBLICKEYBYTES) == 0;

  // validate keys
  bool isValidKeyPair = keyEx->ValidateKeys(keyEx->GetPublicKey(), keyEx->GetPrivateKey());

  // generate pin
  std::string pin = keyEx->GeneratePin();
  bool allDigits = true;
  for(int i = 0; i < pin.length(); i++) {
    if(!isdigit(pin.at(i))) {
      allDigits = false;
      break;
    }
  }
  bool isReasonablePin = pin.length() == PIN_DIGITS && allDigits;
  
  // secondary keys
  KeyExchange* remoteKeys = new KeyExchange(PIN_DIGITS);
  uint8_t* remotePublicKey = remoteKeys->GenerateKeys();

  // set remote keys and calculate secret
  keyEx->SetRemotePublicKey(remotePublicKey);
  remoteKeys->SetRemotePublicKey(publicKey);

  bool successA = keyEx->CalculateSharedKeysServer((unsigned char*)pin.c_str());
  bool successB = remoteKeys->CalculateSharedKeysClient((unsigned char*)pin.c_str());

  bool sharedKeysValid = successA && 
                         successB && 
                         (memcmp(keyEx->GetEncryptKey(), remoteKeys->GetDecryptKey(), crypto_kx_SESSIONKEYBYTES) == 0) && 
                         (memcmp(remoteKeys->GetDecryptKey(), keyEx->GetEncryptKey(), crypto_kx_SESSIONKEYBYTES) == 0);

  ASSERT(publicKeyReturned, "Key from GetPublicKey() does not match public key.");
  ASSERT(isValidKeyPair, "GenerateKeys() returned invalid crypto key pair.");
  ASSERT(isReasonablePin, "Pin is not 6 digits.");
  ASSERT(sharedKeysValid, "Client/server shared keys do not match.");

  delete keyEx;
  delete remoteKeys;

  return true;
}

bool Test_RtsSavedSessions() {
  // load current keys to saved back if necessary
  RtsKeys tmpKeys = SavedSessionManager::LoadRtsKeys();
  RtsKeys testRts = tmpKeys;
  
  // populate with data
  KeyExchange* keyEx = new KeyExchange(6);
  (void)keyEx->GenerateKeys();

  std::copy((uint8_t*)&testRts.keys.id.publicKey, 
            (uint8_t*)&testRts.keys.id.publicKey + crypto_kx_PUBLICKEYBYTES, 
            keyEx->GetPublicKey());

  std::copy((uint8_t*)&testRts.keys.id.privateKey, 
            (uint8_t*)&testRts.keys.id.privateKey + crypto_kx_SECRETKEYBYTES, 
            keyEx->GetPrivateKey());

  keyEx->CalculateSharedKeysServer((const uint8_t*)"123456");

  RtsClientData client;

  std::copy((uint8_t*)&client.publicKey, 
            (uint8_t*)&client.publicKey + crypto_kx_PUBLICKEYBYTES, 
            keyEx->GetPublicKey());

  std::copy((uint8_t*)&client.sessionRx, 
            (uint8_t*)&client.sessionRx + crypto_kx_SESSIONKEYBYTES, 
            keyEx->GetDecryptKey());

  std::copy((uint8_t*)&client.sessionTx, 
            (uint8_t*)&client.sessionTx + crypto_kx_SESSIONKEYBYTES, 
            keyEx->GetEncryptKey());

  testRts.clients.clear();
  testRts.clients.push_back(client);
  testRts.clients.push_back(client);

  SavedSessionManager::SaveRtsKeys(testRts);
  RtsKeys loadedTestRts = SavedSessionManager::LoadRtsKeys();

  // need to verify that after saving/loading, data is same
  bool rtsSessionMatches = false;
  bool clientsMatch = (loadedTestRts.keys.numKnownClients == loadedTestRts.clients.size()) &&
                      (loadedTestRts.clients.size() == testRts.clients.size());

  for(int i = 0; i < loadedTestRts.clients.size(); i++) {
    bool clientMatches = (memcmp(loadedTestRts.clients[i].publicKey, testRts.clients[i].publicKey, crypto_kx_PUBLICKEYBYTES) == 0) &&
      (memcmp(loadedTestRts.clients[i].sessionRx, testRts.clients[i].sessionRx, crypto_kx_SESSIONKEYBYTES) == 0) &&
      (memcmp(loadedTestRts.clients[i].sessionTx, testRts.clients[i].sessionTx, crypto_kx_SESSIONKEYBYTES) == 0);

    clientsMatch &= clientMatches;
  }

  bool keysMatch = (memcmp(loadedTestRts.keys.id.publicKey, testRts.keys.id.publicKey, crypto_kx_PUBLICKEYBYTES) == 0) &&
                   (memcmp(loadedTestRts.keys.id.privateKey, testRts.keys.id.privateKey, crypto_kx_PUBLICKEYBYTES) == 0);

  rtsSessionMatches = clientsMatch && keysMatch;

  // test magic                   
  bool goodMagic = strncmp("ANKIBITS", (char*)loadedTestRts.keys.magic, sizeof(loadedTestRts.keys.magic)) == 0;

  // test name
  bool hasValidName = true;

  if(testRts.keys.id.hasName) {
    std::string productPrefix = "Vector ";
    std::string name = std::string(testRts.keys.id.name);
    if(name.length() != (productPrefix.length() + 4)) {
      hasValidName = false;
    } else {
      bool productMatch = name.compare(0, productPrefix.length(), productPrefix) == 0;

      char* namePtr = (char*)name.c_str() + productPrefix.length();

      bool isAlphaNumAlphaNum = ('A' <= *(namePtr + 0) && *(namePtr + 0) <= 'Z') &&
                                ('0' <= *(namePtr + 1) && *(namePtr + 1) <= '9') &&
                                ('A' <= *(namePtr + 2) && *(namePtr + 2) <= 'Z') &&
                                ('0' <= *(namePtr + 3) && *(namePtr + 3) <= '9');

      hasValidName = productMatch && isAlphaNumAlphaNum;
    }
  }

  // Restore Rts session
  if(tmpKeys.keys.version != -1) {
    SavedSessionManager::SaveRtsKeys(tmpKeys);
  }

  delete keyEx;

  ASSERT(goodMagic, "Bad magic");
  ASSERT(hasValidName, "Victor thinks he has a name, but it does not match the format 'Vector [A-Z][0-9][A-Z][0-9]'.");
  ASSERT(rtsSessionMatches, "Saved Rts session does not match itself after loading again.");

  return true;
}

bool Test_ChristenNameGeneration() {
  const size_t NUM_ITERS = 100;

  bool validNames = true;
  std::string productPrefix = "Vector ";

  for(int i = 0; i < NUM_ITERS; i++) {
    std::string name = Christen::GenerateName();
    
    bool lengthValid = name.length() == (productPrefix.length() + 4);
    ASSERT(lengthValid, "Generated name has invalid length.");
    if(!lengthValid) {
      continue;
    }

    bool productMatch = name.compare(0, productPrefix.length(), productPrefix) == 0;

    char* namePtr = (char*)name.c_str() + productPrefix.length();

    bool isAlphaNumAlphaNum = ('A' <= *(namePtr + 0) && *(namePtr + 0) <= 'Z') &&
                              ('0' <= *(namePtr + 1) && *(namePtr + 1) <= '9') &&
                              ('A' <= *(namePtr + 2) && *(namePtr + 2) <= 'Z') &&
                              ('0' <= *(namePtr + 3) && *(namePtr + 3) <= '9');

    validNames &= (productMatch && isAlphaNumAlphaNum);
  }

  ASSERT(validNames, "There were names generated that did not match expected format 'Vector [A-Z][0-9][A-Z][0-9]'.");

  return true;
}

bool Test_SecurePairing() {
  RtsKeys oldKeys = SavedSessionManager::LoadRtsKeys();

  // Create objects for testing
  Test_INetworkStream* netStream = new Test_INetworkStream();
  std::shared_ptr<Anki::TaskExecutor> taskExecutor = std::make_shared<Anki::TaskExecutor>(ev_default_loop(0));
  Anki::Wifi::Initialize(taskExecutor);
  std::shared_ptr<WifiWatcher> wifiWatcher = std::make_shared<WifiWatcher>(ev_default_loop(0));

  RtsComms* securePairing = new RtsComms(
    netStream,            // 
    ev_default_loop(0),   // ev loop
    nullptr,              // engineClient (don't need--only for updating face)
    nullptr,              // gatewayServer
    nullptr,              // tokenClient
    nullptr,              // connectionIdManager
    wifiWatcher,
    taskExecutor,
    false,                // is pairing
    false,                // is ota-ing
    false);               // has cloud owner

  // Start Test loop
  // Right now this tests will just be a simple runthrough of the
  // messages to form a secure connection.
  //
  netStream->Test(securePairing);

  // cleanup
  SavedSessionManager::SaveRtsKeys(oldKeys);
  delete netStream;
  delete securePairing;

  return true;
}

bool Test_SecurePairingV3() {
  RtsKeys oldKeys = SavedSessionManager::LoadRtsKeys();

  // Create objects for testing
  Test_INetworkStreamV3* netStream = new Test_INetworkStreamV3();
  std::shared_ptr<Anki::TaskExecutor> taskExecutor = std::make_shared<Anki::TaskExecutor>(ev_default_loop(0));
  Anki::Wifi::Initialize(taskExecutor);
  std::shared_ptr<WifiWatcher> wifiWatcher = std::make_shared<WifiWatcher>(ev_default_loop(0));

  RtsComms* securePairing = new RtsComms(
    netStream,            // 
    ev_default_loop(0),   // ev loop
    nullptr,              // engineClient (don't need--only for updating face)
    nullptr,              // gatewayServer
    nullptr,              // tokenClient
    nullptr,              // connectionIdManager
    wifiWatcher,
    taskExecutor,
    false,                // is pairing
    false,                // is ota-ing
    false);               // has cloud owner

  // Start Test loop
  // Right now this tests will just be a simple runthrough of the
  // messages to form a secure connection.
  //
  netStream->Test(securePairing);

  // cleanup
  SavedSessionManager::SaveRtsKeys(oldKeys);
  delete netStream;
  delete securePairing;

  return true;
}

bool Test_SecurePairingV4() {
  RtsKeys oldKeys = SavedSessionManager::LoadRtsKeys();

  // Create objects for testing
  Test_INetworkStreamV4* netStream = new Test_INetworkStreamV4();
  std::shared_ptr<Anki::TaskExecutor> taskExecutor = std::make_shared<Anki::TaskExecutor>(ev_default_loop(0));
  Anki::Wifi::Initialize(taskExecutor);
  std::shared_ptr<WifiWatcher> wifiWatcher = std::make_shared<WifiWatcher>(ev_default_loop(0));

  // securePairing deleted by Test_INetworkStreamV4
  RtsComms* securePairing = new RtsComms(
    netStream,            // 
    ev_default_loop(0),   // ev loop
    nullptr,              // engineClient (don't need--only for updating face)
    nullptr,              // gatewayServer
    nullptr,              // tokenClient
    nullptr,              // connectionIdManager
    wifiWatcher,
    taskExecutor,
    false,                // is pairing
    false,                // is ota-ing
    false);               // has cloud owner

  // Start Test loop
  // Right now this tests will just be a simple runthrough of the
  // messages to form a secure connection.
  //
  netStream->Test(securePairing);

  // cleanup
  SavedSessionManager::SaveRtsKeys(oldKeys);
  delete netStream;

  return true;
}

bool Test_SecurePairingV5() {
  RtsKeys oldKeys = SavedSessionManager::LoadRtsKeys();

  // Create objects for testing
  Test_INetworkStreamV5* netStream = new Test_INetworkStreamV5();
  std::shared_ptr<Anki::TaskExecutor> taskExecutor = std::make_shared<Anki::TaskExecutor>(ev_default_loop(0));
  Anki::Wifi::Initialize(taskExecutor);
  std::shared_ptr<WifiWatcher> wifiWatcher = std::make_shared<WifiWatcher>(ev_default_loop(0));

  // securePairing deleted by Test_INetworkStreamV4
  RtsComms* securePairing = new RtsComms(
    netStream,            // 
    ev_default_loop(0),   // ev loop
    nullptr,              // engineClient (don't need--only for updating face)
    nullptr,              // gatewayServer
    nullptr,              // tokenClient
    nullptr,              // connectionIdManager
    wifiWatcher,
    taskExecutor,
    false,                // is pairing
    false,                // is ota-ing
    false);               // has cloud owner

  // Start Test loop
  // Right now this tests will just be a simple runthrough of the
  // messages to form a secure connection.
  //
  netStream->Test(securePairing);

  // cleanup
  SavedSessionManager::SaveRtsKeys(oldKeys);
  delete netStream;

  return true;
}

int main() {
  TestData tests[] = {
    { Test_ExecCommand,             "ExecCommand not returning expected status code." },
    { Test_BleMessagingProtocol,    "Ble message protocol failed to transmit buffer correctly." },
    { Test_KeyExchange,             "KeyExchange test failed." },
    { Test_RtsSavedSessions,        "SavedSessionManager encountered problem." },
    { Test_ChristenNameGeneration,  "Christening generated invalid name." },
    { Test_SecurePairing,           "SecurePairing V2 failed tests." },
    { Test_SecurePairingV4,         "SecurePairing V4 failed tests." },
    { Test_SecurePairingV5,         "SecurePairing V5 failed tests." }
  };

  int totalPassed = 0;
  int totalTests = sizeof(tests)/sizeof(*tests);
  for(int i = 0; i < totalTests; i++) {
    bool passed = TEST(tests[i].method, tests[i].errorMessage, i);

    if(passed) {
      totalPassed++;
    }
  }

  printf("[%d/%d Tests Passed]\n", totalPassed, totalTests);

  return (totalPassed == totalTests)?0:1;
}

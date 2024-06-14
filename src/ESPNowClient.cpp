/*
Copyright (c) 2023 Valentin Purrucker. All rights reserved.

This work is licensed under the terms of the MIT license.
For a copy, see <https://opensource.org/licenses/MIT> or
the LICENSE file.
*/

#include "ESPNowClient.h"

#include "QuackDebug.h"

#ifdef ESP8266
#include <ESP8266WiFi.h>
#endif
#ifdef ESP32
#include <WiFi.h>
#endif

using QuackMeshESPNow::ReceivedData;
using QuackMeshESPNow::ESPNowClient;
using QuackMeshESPNow::ESPNowSentStatus;

// PUBLIC:

bool QuackMeshESPNow::isAddressMatching(const uint8_t actualAddress[6],
                       const uint8_t expectedAddress[6]) {
  return memcmp(actualAddress, expectedAddress, 6) == 0;
}

ReceivedData::ReceivedData(const uint8_t srcAddress[6], const uint8_t *data,
                           uint8_t dataLength)
    : dataLength(dataLength) {
  memcpy(this->srcAddress, srcAddress, 6);
  memcpy(this->data, data, dataLength);
  memcpy(this->data, data, dataLength);
}

void ESPNowClient::begin() {
  esp_now_init();
  FDEBUG(DEBUG_LEVEL_DEBUG, "Init: %d\n", result);
  initMacAddress();
#ifdef ESP8266
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
#endif

  esp_now_register_recv_cb(ESPNowClient::onDataReceived);
  esp_now_register_send_cb(ESPNowClient::onDataSent);
}

void ESPNowClient::stop() {
  esp_now_unregister_recv_cb();
  esp_now_unregister_send_cb();
  esp_now_deinit();
}

int ESPNowClient::send(uint8_t macAddress[6], uint8_t *data,
                                int dataLength, int maxSendTries, int channel) {
  if (ESPNowClient::WAITING_FOR_DATA_SENT_MUTEX) {
    return -1;
  }

  SendingData newDataToSend = {};
  newDataToSend.dataLength = dataLength;
  newDataToSend.maxTriesLeft = maxSendTries;
  newDataToSend.channel = channel;
  memcpy(newDataToSend.destAddress, macAddress, 6);
  memcpy(newDataToSend.data, data, dataLength);

  mNextDataToSend = std::make_optional(newDataToSend);

  return 0;
}

bool ESPNowClient::sendingPossible() const {
  FDEBUG(DEBUG_LEVEL_DEBUG, "ESPNowClient::sendingPossible, isPossible: %d\n",
                !ESPNowClient::WAITING_FOR_DATA_SENT_MUTEX);
  return !ESPNowClient::WAITING_FOR_DATA_SENT_MUTEX;
}

int ESPNowClient::sendNow(const uint8_t macAddress[6], const uint8_t *data,
                                   int dataLength, uint8_t channel) {
  if (ESPNowClient::WAITING_FOR_DATA_SENT_MUTEX) {
    return -1;
  }

  FDEBUG(DEBUG_LEVEL_DEBUG, "ESPNowClient::sendNow, channel: %d\n", channel);

#ifdef ESP8266
  esp_now_add_peer(const_cast<uint8_t*>(macAddress), ESP_NOW_ROLE_COMBO, channel, NULL, 0);
  int status = esp_now_send(const_cast<uint8_t*>(macAddress), const_cast<uint8_t*>(data), dataLength);
  esp_now_del_peer(const_cast<uint8_t*>(macAddress));
#endif
#ifdef ESP32
  esp_now_peer_info_t peerInfo{};
  memcpy(peerInfo.peer_addr, macAddress, 6);
  peerInfo.channel = channel;
  peerInfo.encrypt = false;
  FDEBUG(DEBUG_LEVEL_DEBUG, "ESPNowClient::sendNow, add peer: %d\n", esp_now_add_peer(&peerInfo));
  int status = esp_now_send(NULL, data, dataLength);
  esp_now_del_peer(macAddress);
  if (status == ESP_OK) {
    DEBUG(DEBUG_LEVEL_DEBUG, "ESPNowClient::sendNow, success\n");
  } else if (status == ESP_ERR_ESPNOW_NOT_INIT) {
    DEBUG(DEBUG_LEVEL_DEBUG, "ESPNowClient::sendNow, not init\n");
  } else if (status == ESP_ERR_ESPNOW_ARG) {
    DEBUG(DEBUG_LEVEL_DEBUG, "ESPNowClient::sendNow, arg\n");
  } else if (status == ESP_ERR_ESPNOW_INTERNAL) {
    DEBUG(DEBUG_LEVEL_DEBUG, "ESPNowClient::sendNow, internal\n");
  } else if (status == ESP_ERR_ESPNOW_NO_MEM) {
    DEBUG(DEBUG_LEVEL_DEBUG, "ESPNowClient::sendNow, no mem\n");
  } else if (status == ESP_ERR_ESPNOW_NOT_FOUND) {
    DEBUG(DEBUG_LEVEL_DEBUG, "ESPNowClient::sendNow, not found\n");
  } else if (status == ESP_ERR_ESPNOW_IF) {
    DEBUG(DEBUG_LEVEL_DEBUG, "ESPNowClient::sendNow, if\n");
  } else {
    DEBUG(DEBUG_LEVEL_DEBUG, "ESPNowClient::sendNow, unknown\n");
  }
#endif
  FDEBUG(DEBUG_LEVEL_DEBUG, "ESPNowClient::sendNow, sent: %d, size: %d\n", status, dataLength);
  if (status == 0) {
    ESPNowClient::WAITING_FOR_DATA_SENT_MUTEX = true;
  } else {
    ESPNowClient::DATA_SENT_UPDATE_MUTEX = true;
    ESPNowClient::LAST_SENT_STATUS = ESPNowSentStatus::Fail;
    ESPNowClient::WAITING_FOR_DATA_SENT_MUTEX = false;
  }

  return status;
}

void ESPNowClient::update() {
  u_long time = millis();

  if (NEW_DATA_RECEIVED_MUTEX) {
    ESPNowClient::RECEIVED_DATA.push(NEW_RECEIVED_DATA);
    NEW_DATA_RECEIVED_MUTEX = false;
  }

  if (ESPNowClient::DATA_SENT_UPDATE_MUTEX) {
    FDEBUG(DEBUG_LEVEL_DEBUG, "ESPNowClient::update, new sent update, status: %d\n",
                  ESPNowClient::LAST_SENT_STATUS);
    if (ESPNowClient::LAST_SENT_STATUS == ESPNowSentStatus::PartialFail) {
      if (mNextDataToSend.value().maxTriesLeft == 0) {
        ESPNowClient::LAST_SENT_STATUS = ESPNowSentStatus::Fail;
      }
    }

    if (ESPNowClient::LAST_SENT_STATUS == ESPNowSentStatus::Fail ||
        ESPNowClient::LAST_SENT_STATUS == ESPNowSentStatus::SendSuccess) {
      if (mOnDataSentCallback) {
        if (isAddressMatching(mNextDataToSend.value().destAddress,
                              BROADCAST_ADDRESS)) {
          ESPNowClient::LAST_SENT_STATUS = ESPNowSentStatus::SendBroadcast;
        }
        mOnDataSentCallback(ESPNowClient::LAST_SENT_STATUS);
      }
      mNextDataToSend = std::nullopt;
      ESPNowClient::WAITING_FOR_DATA_SENT_MUTEX = false;
    }

    ESPNowClient::DATA_SENT_UPDATE_MUTEX = false;
    ESPNowClient::LAST_SENT_STATUS = Undetermined;
  }

  if (!ESPNowClient::DATA_SENT_UPDATE_MUTEX &&
      !ESPNowClient::WAITING_FOR_DATA_SENT_MUTEX && mNextDataToSend.has_value() &&
      time - mLastMessageSentTs >= mMessageSendInterval) {
    mLastMessageSentTs = time;
    const SendingData &dataToSend = mNextDataToSend.value();
    sendNow(dataToSend.destAddress, dataToSend.data, dataToSend.dataLength, dataToSend.channel);
    mNextDataToSend.value().maxTriesLeft -= 1;
  }

  if (time - mLastMessageProcessedTs >= mMessageProcessInterval &&
      !ESPNowClient::WAITING_FOR_DATA_SENT_MUTEX) {
    mMessageProcessInterval = time;
    processMessage();
  }
}

void ESPNowClient::processMessage() {
  if (ESPNowClient::RECIEVED_DATA_ACCESS_MUTEX) {
    return;
  }
  ESPNowClient::RECIEVED_DATA_ACCESS_MUTEX = true;
  if (ESPNowClient::RECEIVED_DATA.empty()) {
    ESPNowClient::RECIEVED_DATA_ACCESS_MUTEX = false;
    return;
  }
  const ReceivedData &firstData = ESPNowClient::RECEIVED_DATA.front();
  ReceivedData data{};
  memcpy(&data, &firstData, sizeof(ReceivedData));
  ESPNowClient::RECEIVED_DATA.pop();
  ESPNowClient::RECIEVED_DATA_ACCESS_MUTEX = false;
  if (mOnDataReceivedCallback) {
    mOnDataReceivedCallback(data);
  }
}

void ESPNowClient::setMessageProcessInterval(u_long interval) {
  mMessageProcessInterval = interval;
}

void ESPNowClient::setOnDataReceivedCallback(
    OnESPNowDataReceivedCallback callback) {
  mOnDataReceivedCallback = callback;
}

void ESPNowClient::setOnDataSentCallback(OnESPNowSentCallback callback) {
  mOnDataSentCallback = callback;
}

String ESPNowClient::getMacAddressAsString() const { return WiFi.macAddress(); }

uint8_t *ESPNowClient::getMACAddress() { return mMACAddress; }

#ifdef ESP8266
void ESPNowClient::onDataReceived(uint8_t *mac_addr, uint8_t *data,
                                  uint8_t data_len) {
  
  DEBUG(DEBUG_LEVEL_DEBUG, "ESPNowClient::onDataReceived\n");
  if (ESPNowClient::RECIEVED_DATA_ACCESS_MUTEX) {
    return;
  }
  ESPNowClient::RECIEVED_DATA_ACCESS_MUTEX = true;
  ESPNowClient::processReceivedData(mac_addr, data, data_len);
  ESPNowClient::RECIEVED_DATA_ACCESS_MUTEX = false;
}
#endif
#ifdef ESP32
void ESPNowClient::onDataReceived(const uint8_t *mac_addr, const uint8_t *data,
                                  int data_len) {
  if (ESPNowClient::RECIEVED_DATA_ACCESS_MUTEX) {
    return;
  }
  ESPNowClient::RECIEVED_DATA_ACCESS_MUTEX = true;
  ESPNowClient::processReceivedData(mac_addr, data, data_len);
  ESPNowClient::RECIEVED_DATA_ACCESS_MUTEX = false;
}
#endif

#ifdef ESP8266
void ESPNowClient::onDataSent(uint8_t *mac_addr, uint8_t status) {
  FDEBUG(DEBUG_LEVEL_DEBUG, "ESPNowClient::onDataSent, status: %d\n", status);
  ESPNowClient::processDataSent(mac_addr, status);
}
#endif
#ifdef ESP32
void ESPNowClient::onDataSent(const uint8_t *mac_addr,
                              esp_now_send_status_t status) {
  FDEBUG(DEBUG_LEVEL_DEBUG, "ESPNowClient::onDataSent, status: %d\n", status);
  ESPNowClient::processDataSent(mac_addr, status);
}
#endif

void ESPNowClient::processReceivedData(const uint8_t *macAddress,
                                       const uint8_t *data,
                                       uint8_t dataLength) {
  if (dataLength < 18) {
    return;
  }
  ReceivedData newReceivedData = ReceivedData(macAddress, data, dataLength);
  ESPNowClient::NEW_RECEIVED_DATA = newReceivedData;
  NEW_DATA_RECEIVED_MUTEX = true;
}

void ESPNowClient::processDataSent(const uint8_t *macAddress,
                                   int status) {
  FDEBUG(DEBUG_LEVEL_DEBUG, "ESPNowClient::processDataSent, status: %d\n", status);
  if (status == 0) {
    ESPNowClient::LAST_SENT_STATUS = ESPNowSentStatus::SendSuccess;
  } else {
    ESPNowClient::LAST_SENT_STATUS = ESPNowSentStatus::PartialFail;
  }
  ESPNowClient::DATA_SENT_UPDATE_MUTEX = true;
}

// PRIVATE:

void ESPNowClient::initMacAddress() {
  #ifdef SOFTAP
  String mac = WiFi.softAPmacAddress();
  #else
  String mac = WiFi.macAddress();
  #endif
  u_int values[6];
  sscanf(mac.c_str(), "%02X:%02X:%02X:%02X:%02X:%02X%*c",
         &values[0], &values[1], &values[2], &values[3], &values[4],
         &values[5]);

  for (size_t i = 0; i < 6; i++) {
    mMACAddress[i] = (uint8_t)values[i];
  }
}

std::queue<ReceivedData> ESPNowClient::RECEIVED_DATA = {};
ReceivedData ESPNowClient::NEW_RECEIVED_DATA = {};
bool ESPNowClient::NEW_DATA_RECEIVED_MUTEX = false;

bool ESPNowClient::RECIEVED_DATA_ACCESS_MUTEX = false;
bool ESPNowClient::WAITING_FOR_DATA_SENT_MUTEX = false;
bool ESPNowClient::DATA_SENT_UPDATE_MUTEX = false;

uint8_t ESPNowClient::BROADCAST_ADDRESS[6] = {0xff, 0xff, 0xff,
                                              0xff, 0xff, 0xff};

ESPNowSentStatus ESPNowClient::LAST_SENT_STATUS =
    ESPNowSentStatus::Undetermined;

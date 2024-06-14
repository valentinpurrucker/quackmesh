/*
Copyright (c) 2023 Valentin Purrucker. All rights reserved.

This work is licensed under the terms of the MIT license.
For a copy, see <https://opensource.org/licenses/MIT> or
the LICENSE file.
*/

#include <Arduino.h>
#include "QuackMeshDevice.h"

#ifdef ESP8266
#include "ESP8266WiFi.h"
#endif
#ifdef ESP32
#include "WiFi.h"
#endif

#include "QuackDebug.h"

using QuackMeshTypes::Acknowledgement;
using QuackMeshTypes::ConfirmedMessage;
using QuackMeshTypes::EnqueuedMessage;
using QuackMeshTypes::EnqueuedMessageType;
using QuackMeshTypes::Message;
using QuackMeshTypes::OnESPNowDataSentStatusCallback;
using QuackMeshTypes::OnNewMessageReceivedCallback;
using QuackMeshTypes::SeenMessageEntry;

using QuackMeshESPNow::ESPNowClient;
using QuackMeshESPNow::ESPNowSentStatus;
using QuackMeshESPNow::isAddressMatching;
using QuackMeshESPNow::ReceivedData;

uint8_t idid;

// PUBLIC:

void QuackMeshDevice::begin() {
  mClient.setOnDataReceivedCallback(std::bind(
      &QuackMeshDevice::onMessageReceived, this, std::placeholders::_1));

  mClient.setOnDataSentCallback(
      std::bind(&QuackMeshDevice::onMessageSent, this, std::placeholders::_1));

  mClient.begin();

  mSeenMessagesCleanupUpdateTs = millis();
  mLastTimeoutCheckTs = millis();
}

void QuackMeshDevice::stop() {
  mClient.setOnDataSentCallback(nullptr);
  mClient.setOnDataReceivedCallback(nullptr);
  mClient.stop();
}

void QuackMeshDevice::update() {
  mClient.update();
  yield();

  updateSeenMessages();
  checkForConfirmationTimeout();
  yield();

  processNextMessage();
}

void QuackMeshDevice::sendMessage(uint8_t data[232], size_t dataLength,
                                  uint8_t destination[6]) {
  enqueueNewMessage(data, dataLength, destination, false);
}

void QuackMeshDevice::sendConfirmedMessage(uint8_t data[232], size_t dataLength,
                                           uint8_t destination[6]) {
  enqueueNewMessage(data, dataLength, destination, true);
}

void QuackMeshDevice::setOnMessageStatusCallback(
    OnESPNowDataSentStatusCallback callback) {
  mSentStatusCallback = callback;
}

void QuackMeshDevice::setOnMessageCallback(
    OnNewMessageReceivedCallback callback) {
  mOnMessageCallback = callback;
}

uint8_t *QuackMeshDevice::getMACAddress() { return mClient.getMACAddress(); }

// PRIVATE:
void QuackMeshDevice::enqueueNewMessage(uint8_t *data, size_t dataLength,
                                        uint8_t destination[6],
                                        bool confirmed) {
  uint8_t networkID[2] = {0, 0};
  Message newMessage = Message(networkID, confirmed ? 1 : 0, getNewMessageId(), 3,
                     getMACAddress(), destination, dataLength, data);

  EnqueuedMessage newEnqueuedMessage {
      .type = confirmed ? EnqueuedMessageType::Confirmed
                        : EnqueuedMessageType::Unconfirmed,
      .channel = 0,
      .message = newMessage};

  mMessageQueue.push(newEnqueuedMessage);
}

void QuackMeshDevice::processNextMessage() {
  if (mMessageSendingInProgress) {
    return;
  }
  if (mMessageQueue.empty()) {
    return;
  } else if (!mClient.sendingPossible()) {
    DEBUG(DEBUG_LEVEL_DEBUG, "MeshDevice::processNextMessage, not possible\n");
    return;
  }

  DEBUG(DEBUG_LEVEL_DEBUG,
        "MeshDevice::processNextMessage\n");

  EnqueuedMessage nextMessage = mMessageQueue.front();

  size_t msgSize = 18 + nextMessage.message.len;

  int sent =
      mClient.send(getMACAddressForDestination(nextMessage.message.destAddress),
                   reinterpret_cast<uint8_t *>(&nextMessage.message), msgSize,
                   2, nextMessage.channel);
  mMessageSendingInProgress = true;
  FDEBUG(DEBUG_LEVEL_DEBUG, "MeshDevice::processNextMessage, sent: %d\n", sent);
  FDEBUG(DEBUG_LEVEL_DEBUG, "MeshDevice::processNextMessage, type: %d\n", nextMessage.type);
  if (sent == 0) {
    DEBUG(DEBUG_LEVEL_DEBUG, "MeshDevice::processNextMessage, sent successfully\n");
    if (nextMessage.type == EnqueuedMessageType::Confirmed) {
      ConfirmedMessage confirmedMessage = {
          .isSent = true,
          .timestamp = 1000,
          .id = nextMessage.message.id,
      };
      memcpy(confirmedMessage.destAddress, nextMessage.message.destAddress, 6);
      mMessagesLeftToConfirm.push_back(confirmedMessage);
    }
  } else {
    mMessageSendingInProgress = false;
    mMessageQueue.pop();
  }
}

void QuackMeshDevice::handleOwnMessage(const Message &message) {
  if (isMessageAlreadySeen(message)) {
    return;
  }

  DEBUG(DEBUG_LEVEL_DEBUG, "MeshDevice::handleOwnMessage, message for me\n");

  rememberMessage(message);

  switch (message.type) {
    case 0:
      if (mOnMessageCallback) {
        mOnMessageCallback(0, message.srcAddress, message.data, message.len);
      }
      break;
    case 1:
      sendAcknowledgement(message);
      if (mOnMessageCallback) {
        mOnMessageCallback(1, message.srcAddress, message.data, message.len);
      }
      break;
    case 3:
      processReceivedAcknowledgement(message);
      break;
    default:
      mOnMessageCallback(message.type, message.srcAddress, message.data,
                         message.len);
  }
}

void QuackMeshDevice::handleForeignMessage(const Message &message) {
  // A MeshDevice will just throw away any message not meant for it
  DEBUG(DEBUG_LEVEL_DEBUG, "MeshDevice::handleForeignMessage, message not for "
                          "me, throwing away\n");
}

void QuackMeshDevice::sendAcknowledgement(const Message &message) {
  uint8_t networkID[2] = {0, 0};
  Message acknowledgementMessage = Message(networkID, 3, message.id, 3, this->getMACAddress(), message.srcAddress, 0, nullptr);

  EnqueuedMessage newEnqueuedMessage {
      .type = EnqueuedMessageType::Acknowledgement,
      .channel = 0,
      .message = acknowledgementMessage
  };

  mMessageQueue.push(newEnqueuedMessage);
}

void QuackMeshDevice::processReceivedAcknowledgement(const Message &message) {
  auto it = mMessagesLeftToConfirm.begin();
  while (it != mMessagesLeftToConfirm.end()) {
    if (it->id == message.id && isAddressMatching(it->destAddress, message.srcAddress)) {
        mMessagesLeftToConfirm.erase(it);
        if (mSentStatusCallback) {
          DEBUG(DEBUG_LEVEL_DEBUG, "QuackMeshDevice::processReceivedAcknowledgement ack\n");
          mSentStatusCallback(ESPNowSentStatus::SendSuccess);
        }
        break;
    }
    it++;
  }
}

void QuackMeshDevice::updateSeenMessages() {
  if (millis() - mSeenMessagesCleanupUpdateTs < mSeenMessagesCleanupInterval) {
    return;
  }

  u_long diff = millis() - mSeenMessagesCleanupUpdateTs;
  mSeenMessagesCleanupUpdateTs = millis();

  auto it = mSeenMessages.begin();
  while (it != mSeenMessages.end()) {
    it->timestamp -= diff;
    if (it->timestamp <= 0) {
      it = mSeenMessages.erase(it);
    } else {
      it++;
    }
  }
}

bool QuackMeshDevice::isMessageAlreadySeen(const Message &message) {
  EnqueuedMessageType type;
  if (message.type == 0) {
    type = EnqueuedMessageType::Unconfirmed;
  } else if (message.type == 1) {
    type = EnqueuedMessageType::Confirmed;
  } else if (message.type == 3)  {
    type = EnqueuedMessageType::Acknowledgement;
  } else {
    type = EnqueuedMessageType::Forwarded;
  }

  auto it = mSeenMessages.begin();
  while (it != mSeenMessages.end()) {
    if (it->timestamp > 0 && it->id == message.id &&
        isAddressMatching(it->srcAddress, message.srcAddress) &&
        isAddressMatching(it->destAddress, message.destAddress) && 
        it->type == type) {
      return true;
    }
    it++;
  }
  return false;
}

void QuackMeshDevice::checkForConfirmationTimeout() {
  u_long diff = millis() - mLastTimeoutCheckTs;
  mLastTimeoutCheckTs = millis();

  auto it = mMessagesLeftToConfirm.begin();
  while (it != mMessagesLeftToConfirm.end()) {
    it->timestamp -= diff;
    if (it->timestamp <= 0) {
      it = mMessagesLeftToConfirm.erase(it);
      if (mSentStatusCallback) {
        mSentStatusCallback(ESPNowSentStatus::Fail);
      }
    } else {
      it++;
    }
  }
}

uint8_t QuackMeshDevice::getNewMessageId() { return idid++; }

uint8_t *QuackMeshDevice::getMACAddressForDestination(uint8_t destination[6]) {
  return ESPNowClient::BROADCAST_ADDRESS;
}

void QuackMeshDevice::onMessageReceived(ReceivedData data) {
  Message message = {};

  DEBUG(DEBUG_LEVEL_DEBUG, "MeshDevice::onMessageReceived, received message\n");

  memcpy(&message, data.data, data.dataLength);

  // Debug print the message struct
  DEBUG(DEBUG_LEVEL_DEBUG, "Received Message:\n");
  DEBUG(DEBUG_LEVEL_DEBUG, "Type: ");
  FDEBUG(DEBUG_LEVEL_DEBUG, "%d\n", message.type);
  DEBUG(DEBUG_LEVEL_DEBUG, "ID: ");
  FDEBUG(DEBUG_LEVEL_DEBUG, "%d\n", message.id);
  DEBUG(DEBUG_LEVEL_DEBUG, "Hop Count: ");
  FDEBUG(DEBUG_LEVEL_DEBUG, "%d\n", message.hopCount);
  DEBUG(DEBUG_LEVEL_DEBUG, "Length: ");
  FDEBUG(DEBUG_LEVEL_DEBUG, "%d\n", message.len);
  DEBUG(DEBUG_LEVEL_DEBUG, "Source Address: ");
  for (int i = 0; i < 6; i++) {
    FDEBUG(DEBUG_LEVEL_DEBUG, "%02X", message.srcAddress[i]);
    DEBUG(DEBUG_LEVEL_DEBUG, " ");
  }
  DEBUG(DEBUG_LEVEL_DEBUG, "\n");
  DEBUG(DEBUG_LEVEL_DEBUG, "Destination Address: ");
  for (int i = 0; i < 6; i++) {
    FDEBUG(DEBUG_LEVEL_DEBUG, "%02X", message.destAddress[i]);
    DEBUG(DEBUG_LEVEL_DEBUG, " ");
  }
  DEBUG(DEBUG_LEVEL_DEBUG, "\n");

  if (isAddressMatching(message.destAddress, mClient.getMACAddress())) {
    handleOwnMessage(message);
  } else {
    handleForeignMessage(message);
  }
}

void QuackMeshDevice::onMessageSent(ESPNowSentStatus status) {
  FDEBUG(DEBUG_LEVEL_DEBUG, "MeshDevice::onMessageSent status %d\n", status);
  if (mMessageQueue.front().type == EnqueuedMessageType::Confirmed) {
    if (status == ESPNowSentStatus::Fail) {
      auto it = mMessagesLeftToConfirm.begin();
      while (it != mMessagesLeftToConfirm.end()) {
        if (it->id == mMessageQueue.front().message.id &&
            isAddressMatching(it->destAddress,
                              mMessageQueue.front().message.destAddress)) {
          mMessagesLeftToConfirm.erase(it);
          break;
        }
        it++;
      }
      if (mSentStatusCallback) {
        mSentStatusCallback(status);
      }
    }
  } else {
    FDEBUG(DEBUG_LEVEL_DEBUG, "MeshDevice::onMessageSent, status: %d\n", status);
  }

  mMessageSendingInProgress = false;
  mMessageQueue.pop();
}

void QuackMeshDevice::rememberMessage(const Message &message) {
  if (isMessageAlreadySeen(message)) {
    return;
  }

  EnqueuedMessageType type;
  if (message.type == 0) {
    type = EnqueuedMessageType::Unconfirmed;
  } else if (message.type == 1) {
    type = EnqueuedMessageType::Confirmed;
  } else if (message.type == 3)  {
    type = EnqueuedMessageType::Acknowledgement;
  } else {
    type = EnqueuedMessageType::Forwarded;
  }

  SeenMessageEntry newSeenMessage{.id = message.id,
                                  .srcAddress = {},
                                  .destAddress = {},
                                  .timestamp = mSeenMessagesCleanupTimeout,
                                  .type = type
                                  };

  memcpy(newSeenMessage.srcAddress, message.srcAddress, 6);
  memcpy(newSeenMessage.destAddress, message.destAddress, 6);

  if (mSeenMessages.size() >= mMaxSeenMessagesQueueSize) {
    mSeenMessages.erase(mSeenMessages.begin());
  }

  mSeenMessages.push_back(newSeenMessage);
}

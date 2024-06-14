/*
Copyright (c) 2023 Valentin Purrucker. All rights reserved.

This work is licensed under the terms of the MIT license.
For a copy, see <https://opensource.org/licenses/MIT> or
the LICENSE file.
*/

#pragma once

#include <Arduino.h>

namespace QuackMeshTypes {

// The callback that is called when a message is sent
typedef std::function<void(int)> OnESPNowDataSentStatusCallback;

// The callback that is called when a message is received
typedef std::function<void(uint8_t type, const uint8_t srcAddress[6],
                           const uint8_t *data, size_t dataLength)>
    OnNewMessageReceivedCallback;

struct Message {
  uint8_t networkID[2] = {0};
  uint8_t type = 0;
  uint8_t id = 0;
  uint8_t hopCount = 0;
  uint8_t srcAddress[6] = {};
  uint8_t destAddress[6] = {};
  uint8_t len = 0;
  uint8_t data[232] = {};

  Message() = default;

  Message(uint8_t networkID[2], uint8_t type, uint8_t id, uint8_t hopCount,
          const uint8_t srcAddress[6], const uint8_t destAddress[6], uint8_t len,
          const uint8_t data[232]) {
    memcpy(this->networkID, networkID, 2);
    this->type = type;
    this->id = id;
    this->hopCount = hopCount;
    memcpy(this->srcAddress, srcAddress, 6);
    memcpy(this->destAddress, destAddress, 6);
    this->len = len;
    memcpy(this->data, data, len);
  }
};

struct ConfirmedMessage {
  bool isSent;
  int16_t timestamp;
  uint8_t id;
  uint8_t destAddress[6];
};

enum EnqueuedMessageType {
  Unconfirmed,
  Confirmed,
  Forwarded,
  Acknowledgement,
};

struct EnqueuedMessage {
  EnqueuedMessageType type;
  int channel;
  Message message;
};

struct SendingMessage {
  bool isSent;
  bool needsConfirmation;
  int16_t timestamp;
  Message message;
};

/**
 * This struct is used to store messages that the client already saw
 */
struct SeenMessageEntry {
  uint8_t id;
  uint8_t srcAddress[6];
  uint8_t destAddress[6];
  u_long timestamp;
  EnqueuedMessageType type;
};

/**
 * This struct is used to store routing information about nodes in the network
 */
struct RoutingEntry {
  uint8_t destination[6];
  uint8_t link[6];
  uint8_t hops;
  int16_t timestamp;
};
}  // namespace QuackMeshTypes

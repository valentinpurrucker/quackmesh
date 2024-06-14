/*
Copyright (c) 2023 Valentin Purrucker. All rights reserved.

This work is licensed under the terms of the MIT license.
For a copy, see <https://opensource.org/licenses/MIT> or
the LICENSE file.
*/

#include "QuackMeshRouter.h"

#include "ESPNowClient.h"

#include "QuackDebug.h"

using QuackMeshESPNow::ESPNowClient;
using QuackMeshESPNow::isAddressMatching;

using QuackMeshTypes::EnqueuedMessage;
using QuackMeshTypes::EnqueuedMessageType;
using QuackMeshTypes::Message;
using QuackMeshTypes::RoutingEntry;

// PUBLIC:

void QuackMeshRouter::begin() {
  DEBUG(DEBUG_LEVEL_DEBUG, "QuackMeshRouter::begin\n");
  QuackMeshDevice::begin();

  mLastRoutingTableUpdateTs = millis();
}

void QuackMeshRouter::update() {
  QuackMeshDevice::update();

  updateRoutingTable();
}

// PRIVATE:

void QuackMeshRouter::handleForeignMessage(const Message &message) {
  DEBUG(DEBUG_LEVEL_DEBUG, "Process Foreign message\n");
  forwardMessage(message);
}

void QuackMeshRouter::addOrUpdateRoutingInfo(uint8_t destination[6],
                                             uint8_t link[6], uint8_t hops) {
  std::vector<RoutingEntry>::iterator oldestEntry = mRoutingTable.begin();

  auto it = mRoutingTable.begin();
  while (it != mRoutingTable.end()) {
    if (isAddressMatching(it->destination, destination)) {
      if (hops < it->hops) {
        it->hops = hops;
        it->timestamp = mRoutingTableUpdateTimeout;
        memcpy(it->link, link, 6);

        oldestEntry = mRoutingTable.end();
        return;
      }

      // Find the oldest entry
      if (it->timestamp < oldestEntry->timestamp) {
        oldestEntry = it;
      }
    }
    it++;
  }

  /*
   * If the routing table is full and we didn't find an entry to update
   * remove the oldest entry
   */
  RoutingEntry newRoutingInfo{.destination = {},
                              .link = {},
                              .hops = hops,
                              .timestamp = static_cast<int16_t>(mRoutingTableUpdateTimeout)};

  memcpy(newRoutingInfo.destination, destination, 6);
  memcpy(newRoutingInfo.link, link, 6);

  mRoutingTable.push_back(newRoutingInfo);
}

void QuackMeshRouter::forwardMessage(const Message &message) {
  DEBUG(DEBUG_LEVEL_DEBUG, "Process Forwarding message\n");
  if (message.hopCount - 1 == 0) {
    return;
  }

  if (isMessageAlreadySeen(message)) {
    return;
  }

  rememberMessage(message);


  uint8_t networkID[2] = {0, 0};
  Message forwardingMessage(networkID, message.type, message.id,
                            message.hopCount - 1, message.srcAddress,
                            message.destAddress, message.len, message.data);

  EnqueuedMessage newEnqueuedMessage{.type = EnqueuedMessageType::Forwarded,
                                     .message = forwardingMessage};

  mMessageQueue.push(newEnqueuedMessage);
}

void QuackMeshRouter::updateRoutingTable() {
  if (millis() - mLastRoutingTableUpdateTs < mRoutingTableUpdateInterval) {
    return;
  }

  u_long diff = millis() - mLastRoutingTableUpdateTs;
  mLastRoutingTableUpdateTs = millis();

  auto it = mRoutingTable.begin();

  while (it != mRoutingTable.end()) {
    it->timestamp -= diff;
    if (it->timestamp <= 0) {
      it = mRoutingTable.erase(it);
    } else {
      it++;
    }
  }
}

uint8_t *QuackMeshRouter::getMACAddressForDestination(uint8_t destination[6]) {
  auto it = mRoutingTable.begin();
  while (it != mRoutingTable.end()) {
    if (isAddressMatching(destination, it->destination)) {
      return it->link;
    }
    it++;
  }
  return ESPNowClient::BROADCAST_ADDRESS;
}

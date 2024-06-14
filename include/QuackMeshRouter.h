/*
Copyright (c) 2023 Valentin Purrucker. All rights reserved.

This work is licensed under the terms of the MIT license.
For a copy, see <https://opensource.org/licenses/MIT> or
the LICENSE file.
*/

#pragma once

#include <Arduino.h>

#include "QuackMeshDevice.h"

/**
 * This class represents a Mesh-Router
 * A router is also a Mesh-Device but in addition is able to
 * forward messages to other devices
 * and do proper routing of messages
 */
class QuackMeshRouter : public QuackMeshDevice {
 public:
    void begin();
  void update();

 protected:
  void handleForeignMessage(const QuackMeshTypes::Message &message);

  /**
   * Add or update a routing entry
   * @param destination The MAC-Address of the destination
   * @param link The MAC-Address of the link to the destination
   * @param hops The number of hops to the destination
   */
  void addOrUpdateRoutingInfo(uint8_t destination[6], uint8_t link[6],
                              uint8_t hops);

  /**
   * Forward a message to the next hop
   * @param message The message to be forwarded
   */
  void forwardMessage(const QuackMeshTypes::Message &message);

  /**
   * Update the routing table and update its state
   * This method is called periodically and removes any entries that are too old
   */
  void updateRoutingTable();

  uint8_t *getMACAddressForDestination(uint8_t destination[6]);

  u_long mLastRoutingTableUpdateTs =
      0;  // The timestamp of the last routing table update
  u_long mRoutingTableUpdateInterval =
      100;  // The interval in which the routing table is updated in
            // milliseconds
  u_long mRoutingTableUpdateTimeout =
      10000;  // The timeout after which a routing entry is removed in
             // milliseconds

  std::vector<QuackMeshTypes::RoutingEntry> mRoutingTable =
      {};  // The routing table

  size_t mMaxRoutingEntries = 10;  // The maximum number of routing entries
};

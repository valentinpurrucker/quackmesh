/*
Copyright (c) 2023 Valentin Purrucker. All rights reserved.

This work is licensed under the terms of the MIT license.
For a copy, see <https://opensource.org/licenses/MIT> or
the LICENSE file.
*/

#pragma once

#include <Arduino.h>

#include <functional>
#include <queue>
#include <vector>

#include "ESPNowClient.h"
#include "QuackMeshTypes.h"

/**
 * A Mesh-Device is a device that is able to send and receive messages over
 * ESP-Now and communicate in a mesh network with other devices.
 * It uses the implementation of a ESPNowClient to send and receive messages.
 */
class QuackMeshDevice {
 public:
  /**
   * Start the Mesh device
   */
  void begin();

  void stop();

  /**
   * Update the Mesh device state
   */
  void update();

  /**
   * Enqueue a new unconfirmed-message to be sent
   * @param data The data to be sent
   * @param dataLength The length of the data to be sent
   * @param destination The MAC-Address of the destination
   */
  void sendMessage(uint8_t data[232], size_t dataLength,
                   uint8_t destination[6]);

  /**
   * Enqueue a new confirmed-message to be sent
   * @param data The data to be sent
   * @param dataLength The length of the data to be sent
   * @param destination The MAC-Address of the destination
   */
  void sendConfirmedMessage(uint8_t data[232], size_t dataLength,
                            uint8_t destination[6]);

  /**
   * Set the callback that is called when a message is sent
   * @param callback The callback to be called
   */
  void setOnMessageStatusCallback(
      QuackMeshTypes::OnESPNowDataSentStatusCallback callback);

  /**
   * Set the callback that is called when a message is received
   * @param callback The callback to be called
   */
  void setOnMessageCallback(
      QuackMeshTypes::OnNewMessageReceivedCallback callback);

  uint8_t *getMACAddress();

 protected:
  /**
   * This method enqueues a new message
   * @param data The data to be sent
   * @param dataLength The length of the data to be sent
   * @param destination The MAC-Address of the destination
   * @param confirmed Whether the message should be confirmed or not
   */
  void enqueueNewMessage(uint8_t *data, size_t dataLength,
                         uint8_t destination[6], bool confirmed);

  /**
   * This method processes the next message in the queue of messages to be sent
   */
  void processNextMessage();

  /**
   * Process the given message that is sent to this device
   * @param message The message to be processed
   */
  void handleOwnMessage(const QuackMeshTypes::Message &message);

  /**
   * Process the given message that is sent to another device
   * @param message The message to be processed
   */
  virtual void handleForeignMessage(const QuackMeshTypes::Message &message);

  /**
   * Send an acknowledgement for the given message
   * @param message The message to be acknowledged
   */
  void sendAcknowledgement(const QuackMeshTypes::Message &message);

  /**
   * Process the given acknowledgement
   * @param message The acknowledgement to be processed
   */
  void processReceivedAcknowledgement(const QuackMeshTypes::Message &message);

  /**
   * Update the last-seen messages store
   */
  void updateSeenMessages();

  /**
   * Check whether the given message was already seen
   * @param message The message to be checked
   * @return Whether the message was already seen
   */
  bool isMessageAlreadySeen(const QuackMeshTypes::Message &message);

  /**
   * Update and check if messages to be confirmed timed-out and call
   * the corresponding callbacks
   */
  void checkForConfirmationTimeout();

  /**
   * Generate a message id for the next message.
   * It is up to the implementation to make sure that the message id is unique
   * or have a high enough probability of being unique
   */
  uint8_t getNewMessageId();

  /**
   * Get the MAC-Address where a message has to be sent to reach destination
   * @param destination The MAC-Address of the destination
   * @return The MAC-Address where a message has to be sent to reach destination
   */
  uint8_t *getMACAddressForDestination(uint8_t destination[6]);

  /**
   * Callback that is called when a message is received
   * @param data The data that was received
   */
  void onMessageReceived(QuackMeshESPNow::ReceivedData data);

  /**
   * Callback that is called when a message is sent
   * @param status The status of the sent message
   */
  void onMessageSent(QuackMeshESPNow::ESPNowSentStatus status);

  void rememberMessage(const QuackMeshTypes::Message &message);

  std::queue<QuackMeshTypes::EnqueuedMessage> mMessageQueue =
      {};  // The queue of messages to be sent

  std::vector<QuackMeshTypes::ConfirmedMessage> mMessagesLeftToConfirm =
      {};  // The messages that are waiting for an acknowledgement

  std::vector<QuackMeshTypes::SeenMessageEntry> mSeenMessages =
      {};  // The messages that were already seen

  size_t mMaxSeenMessagesQueueSize = 10;  // The maximum number of seen messages

  u_long mSeenMessagesCleanupUpdateTs =
      0;  // The timestamp of the last seen messages cleanup
  u_long mSeenMessagesCleanupInterval =
      1000;  // The interval in which the seen messages are cleaned up
  u_long mSeenMessagesCleanupTimeout =
      2000;  // The timeout after which a seen message is removed

  u_long mLastTimeoutCheckTs = 0;  // The timestamp of the last timeout check
                                   // for messages to be confirmed

  bool mMessageSendingInProgress =
      false;  // Whether a message is currently being sent

  QuackMeshESPNow::ESPNowClient mClient = {};  // The ESPNow client

  QuackMeshTypes::OnESPNowDataSentStatusCallback mSentStatusCallback =
      nullptr;  // The callback that is called when a message is sent

  QuackMeshTypes::OnNewMessageReceivedCallback mOnMessageCallback =
      nullptr;  // The callback that is called when a message is received
};

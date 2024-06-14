/*
Copyright (c) 2023 Valentin Purrucker. All rights reserved.

This work is licensed under the terms of the MIT license.
For a copy, see <https://opensource.org/licenses/MIT> or
the LICENSE file.
*/

#pragma once

#include <Arduino.h>

#include <functional>
#include <optional>
#include <queue>

#ifdef ESP8266
#include <espnow.h>
#endif
#ifdef ESP32
#include <esp_now.h>
#endif

namespace QuackMeshESPNow {

struct ReceivedData {
  uint8_t srcAddress[6] = {};
  uint8_t data[250] = {};
  uint8_t dataLength = 0;

  /**
   * Constructor
   * Init fields to its default values
   */
  ReceivedData() = default;

  /**
   * Constructor
   * @param srcAddress The MAC-Address of the sender
   * @param data The data that was received
   * @param dataLength The length of the data that was received
   */
  ReceivedData(const uint8_t srcAddress[6], const uint8_t data[250],
               uint8_t dataLength);
};

/**
 * These are the possible status that the sent callback will receive
 * Undetermined: No messages have been sent yet
 * Success: The message reached the MAC-Layer of the destination
 * Broadcast: Since broadcasting doesn't provide SENT-Callbacks, this value is
 * passed instead
 * PartialFail: The message did not reach the MAC-Layer of the
 * destination, BUT there are still SEND-TRIES left
 * Fail: Sending the message
 * failed.
 */
enum ESPNowSentStatus {
  Undetermined = 0,
  SendSuccess,
  SendBroadcast,
  PartialFail,
  Fail
};

typedef std::function<void(ReceivedData)>
    OnESPNowDataReceivedCallback;  // Callback for new received data
typedef std::function<void(ESPNowSentStatus)>
    OnESPNowSentCallback;  // Callback for new sent status update

/**
 * This struct is used to store the data that is to be sent
 */
struct SendingData {
  uint8_t destAddress[6];
  uint8_t data[250];
  uint8_t dataLength;
  uint8_t maxTriesLeft;
  int channel;
};

// Checks if the given addresses are equal
bool isAddressMatching(const uint8_t actualAddress[6],
                       const uint8_t expectedAddress[6]);

/**
 * This class provides a arduino-like interface for sending and receiving data
 * over ESP-Now
 */
class ESPNowClient {
 public:
  /**
   * This method initializes the ESP-Now-Client
   */
  void begin();

  void stop();

  /**
   * This method enqueues a new send for the given data to the given MAC-Address
   * @param macAddress The MAC-Address of the destination
   * @param data The data to be sent
   * @param dataLength The length of the data to be sent
   * @param maxSendTries The maximum number of tries to send the message on the
   * link-layer
   * @param channel The wifi channel to send the message on
   * @return status indicating whether the message was successfully queued
   */
  int send(uint8_t macAddress[6], uint8_t *data, int dataLength,
           int maxSendTries, int channel);

  /**
   * Checks if the client is able to queue a new message
   * @return
   */
  bool sendingPossible() const;

  /**
   * This method is used to update the ESP-Now-Client
   */
  void update();

  /**
   * This method sets the interval in which the ESP-Now-Client will process the
   * next message
   * @param interval The interval in milliseconds
   */
  void setMessageProcessInterval(u_long interval);

  /**
   * Set the callback for when a new message arrived
   * @param callback The callback to be called
   */
  void setOnDataReceivedCallback(OnESPNowDataReceivedCallback callback);

  /**
   * Set the callback for when a new status update for a sent message is
   * available
   * @param callback The callback to be called
   */
  void setOnDataSentCallback(OnESPNowSentCallback callback);

  /**
   * Get the MAC-Address of the ESP-Now-Client as a String
   * @return The MAC-Address as a String
   */
  String getMacAddressAsString() const;

  /**
   * Get the MAC-Address of the ESP-Now-Client as a byte array
   * @return The MAC-Address as a byte array
   */
  uint8_t *getMACAddress();

  /**
   * This address represents the broadcast address for ESP-Now
   */
  static uint8_t BROADCAST_ADDRESS[6];

 private:
  /**
   * This is the ISR that is called when a new message is received
   * @param mac_addr The MAC-Address of the sender
   * @param data The data that was sent
   * @param data_len The length of the data that was sent
   */
#ifdef ESP8266
  static void IRAM_ATTR onDataReceived(uint8_t *mac_addr, uint8_t *data,
                                       uint8_t data_len);
#endif
#ifdef ESP32
  static void IRAM_ATTR onDataReceived(const uint8_t *mac_addr,
                                       const uint8_t *data, int data_len);
#endif

  /**
   * This method is called when a new message is received
   * @param macAddress The MAC-Address of the sender
   * @param data The data that was received
   * @param dataLength The length of the data that was received
   */
  static void IRAM_ATTR processReceivedData(const uint8_t *macAddress,
                                            const uint8_t *data,
                                            uint8_t dataLength);

  /**
   * This method is called when a new status update for a sent message is
   * available
   * @param macAddress The MAC-Address of the destination
   * @param status The status of the sent message
   */
  static void IRAM_ATTR processDataSent(const uint8_t *macAddress, int status);

  /**
   * This is the ISR when a new message is sent and the client has a new
   * sent-status update
   */
#ifdef ESP8266
  static void IRAM_ATTR onDataSent(uint8_t *mac_addr, uint8_t status);
#endif
#ifdef ESP32
  static void IRAM_ATTR onDataSent(const uint8_t *mac_addr,
                                   esp_now_send_status_t status);
#endif

  /**
   * This method initializes the MAC-Address of the ESP-Now-Client
   */
  void initMacAddress();

  /**
   * This method sends the next message in the queue
   * @param macAddress The MAC-Address of the destination
   * @param data The data to be sent
   * @param dataLength The length of the data to be sent
   * @param channel The wifi channel to send the message on
   */
  int sendNow(const uint8_t macAddress[6], const uint8_t *data, int dataLength,
              uint8_t channel);

  /**
   * This method processes the next message in the queue of received messages
   */
  void processMessage();

  static std::queue<ReceivedData>
      RECEIVED_DATA;  // The queue of received messages

  static ReceivedData NEW_RECEIVED_DATA;  // The new received message

  static bool RECIEVED_DATA_ACCESS_MUTEX;  // Mutex for accessing the queue of
                                           // received messages

  static bool WAITING_FOR_DATA_SENT_MUTEX;  // Mutex for waiting for a new
                                            // status update for a sent message

  static bool DATA_SENT_UPDATE_MUTEX;  // Mutex for updating the status of a
                                       // sent message

  static bool
      NEW_DATA_RECEIVED_MUTEX;  // Mutex for accessing the new received message

  static ESPNowSentStatus
      LAST_SENT_STATUS;  // The last status of a sent message

  std::optional<SendingData> mNextDataToSend =
      std::nullopt;  // The next message to be sent

  u_long mMessageProcessInterval =
      0;  // The interval in which the next message is processed
  u_long mLastMessageProcessedTs =
      0;  // The timestamp of the last message processed
  u_long mMessageSendInterval =
      100;  // The interval in which the next message is sent
  u_long mLastMessageSentTs = 0;  // The timestamp of the last message sent

  uint8_t mMACAddress[6] = {};  // The MAC-Address of the ESP-Now-Client

  OnESPNowDataReceivedCallback
      mOnDataReceivedCallback;  // The callback for when a new message is
                                // received
  OnESPNowSentCallback
      mOnDataSentCallback;  // The callback for when a new status update for a
                            // sent message is available
};
}  // namespace QuackMeshESPNow

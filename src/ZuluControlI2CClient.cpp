/**
 * ZuluIDE™ - Copyright (c) 2023 Rabbit Hole Computing™
 *
 * ZuluIDE™ firmware is licensed under the GPL version 3 or any later version.
 *
 * https://www.gnu.org/licenses/gpl-3.0.html
 * ----
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 **/

#include "ZuluControlI2CClient.h"
#include "fw_upgrade.h"
#include <hardware/sync.h>
#include <pico/time.h>
namespace zuluide::i2c::client {

static volatile Packet* current = nullptr;
static queue_t outputQueue;
static queue_t inputQueue;
static queue_t availInputQueue;

static void i2c_slave_handler(i2c_inst_t* i2c, i2c_slave_event_t event) {
   switch (event) {
      case I2C_SLAVE_RECEIVE: {
         if (current == nullptr) {
            // Get a buffer.
            static bool input_queue_removed = true;
            if (queue_try_remove(&availInputQueue, &current)) {
               input_queue_removed = true;
            }
            else if (input_queue_removed)
            {
               printf("Unable to get a free buffer\n");
               input_queue_removed = false;
            }
         }

         if (current->state == ReceiveState::None) {
            if (i2c_get_read_available(i2c0) > 0) {
               current->command = i2c_read_byte_raw(i2c0);
               current->state = ReceiveState::SentCommand;
            }
         } else if (current->state == ReceiveState::SentCommand) {
            if (current->pos == 0) {
               if (i2c_get_read_available(i2c0) > 1) {
                  // Both length bytes at once.
                  current->lengthBytes[0] = i2c_read_byte_raw(i2c0);
                  current->lengthBytes[1] = i2c_read_byte_raw(i2c0);
                  current->state = ReceiveState::SentLength;
                  current->pos = 0;
                  current->length = (current->lengthBytes[0] << 8) | current->lengthBytes[1];
                  if (current->length == 0) {
                     // We have now received the entire message.
                     queue_try_add(&inputQueue, &current);
                     current = nullptr;
                  }
               } else if (i2c_get_read_available(i2c0) > 0) {
                  // Received the first length byte.
                  current->lengthBytes[0] = i2c_read_byte_raw(i2c0);
                  current->pos++;
               }
            } else if (i2c_get_read_available(i2c0) > 0) {
               // Received the second length byte.
               current->lengthBytes[1] = i2c_read_byte_raw(i2c0);
               current->state = ReceiveState::SentLength;
               current->pos = 0;
               current->length = (current->lengthBytes[0] << 8) | current->lengthBytes[1];

               if (current->length == 0) {
                  // We have now received the entire message.
                  queue_try_add(&inputQueue, &current);
                  current = nullptr;
               }
            }
         } else if (current->state == ReceiveState::SentLength) {
            // Read string data.
            while (current->pos < current->length && i2c_get_read_available(i2c0) > 0) {
               current->buffer[current->pos++] = i2c_read_byte_raw(i2c0);
            }

            if (current->pos == current->length) {
               // We have now received the entire message.
               queue_try_add(&inputQueue, &current);
               current = nullptr;
            }
         }

         break;
      }
      case I2C_SLAVE_REQUEST: {
         if (current != nullptr) {
            // A write transaction was left incomplete (e.g. the master gave up
            // partway through). Discard it so stale state doesn't get
            // mistaken for the start of the next message.
            current->length = 0;
            current->pos = 0;
            current->state = ReceiveState::None;
            memset((void*)current->buffer, 0, MAX_MSG_SIZE);
            queue_try_add(&availInputQueue, &current);
            current = nullptr;
         }

         Packet* toSend;
         if (queue_try_peek(&outputQueue, &toSend)) {
            if (toSend->state == ReceiveState::None) {
               uint8_t buffer[3];
               buffer[0] = toSend->command;
               buffer[1] = toSend->lengthBytes[0];
               buffer[2] = toSend->lengthBytes[1];
               i2c_write_raw_blocking(i2c0, buffer, 3);
               if (toSend->length == 0) {
                  // Cleanup, sent a request without a string payload.
                  if (!queue_try_remove(&outputQueue, &toSend)) {
                     printf("Unable to remove from queue.\n");
                  }
                     delete toSend;
                  } else {
                  toSend->state = ReceiveState::SentLength;
               }
            } else if (toSend->state == ReceiveState::SentLength) {
               // Send out the message.
               if ((toSend->length - toSend->pos) > BUFFER_LENGTH) {
                  i2c_write_raw_blocking(i2c0, toSend->buffer + toSend->pos, BUFFER_LENGTH);
                  toSend->pos += BUFFER_LENGTH;
                  // Leave at the top of the queue for the next I2C_SLAVE_REQUEST
               } else {
                  i2c_write_raw_blocking(i2c0, toSend->buffer + toSend->pos, toSend->length - toSend->pos);

                  // Cleanup.
                  queue_try_remove(&outputQueue, &toSend);
                  delete toSend;
               }
            }
         } else {
            // Send NOOP for the
            uint8_t buffer[3] = {0};
            buffer[0] = I2C_CLIENT_NOOP;
            i2c_write_raw_blocking(i2c0, buffer, 3);
         }
         break;
      }
      case I2C_SLAVE_FINISH: {
         break;
      }
      default:
         break;
   }
}

bool EnqueueRequest(uint8_t request) {
   if (request == I2C_CLIENT_RESET_QUEUE) {
      // Clear the output queue.
      Packet* toDelete;
      while (queue_try_remove(&outputQueue, &toDelete)) {
         delete toDelete;
      }
      return true;
   }

   Packet* p = new Packet();
   p->length = 0;
   p->command = request;
   p->pos = 0;
   p->state = ReceiveState::None;
   if (!queue_try_add(&outputQueue, &p)) {
      delete p;
      return false;
   }
   return true;
}

bool EnqueueRequest(uint8_t request, const char* toSend) {
   if (queue_is_full(&outputQueue))
   {
      EnqueueRequest(I2C_CLIENT_RESET_QUEUE);
   }
   Packet* p = new Packet();
   p->command = request;
   p->length = strlen(toSend);
   p->lengthBytes[0] = p->length >> 8;
   p->lengthBytes[1] = p->length;
   p->pos = 0;
   p->state = ReceiveState::None;
   memcpy(p->buffer, toSend, p->length);
   if (!queue_try_add(&outputQueue, &p)) {
      delete p;
      return false;
   }
   return true;
}

void WaitForOutputQueueDrain(uint32_t timeout_ms) {
   uint32_t start = to_ms_since_boot(get_absolute_time());
   while (!queue_is_empty(&outputQueue)) {
      if ((uint32_t)(to_ms_since_boot(get_absolute_time()) - start) >= timeout_ms) {
         break;
      }
      sleep_ms(1);
   }
}

bool EnqueueRequestBinary(uint8_t request, const uint8_t* payload, uint16_t length) {
   if (payload == nullptr) {
      return false;
   }

   if (queue_is_full(&outputQueue))
   {
      EnqueueRequest(I2C_CLIENT_RESET_QUEUE);
   }
   if (length > MAX_MSG_SIZE) length = MAX_MSG_SIZE;
   Packet* p = new Packet();
   p->command = request;
   p->length = length;
   p->lengthBytes[0] = p->length >> 8;
   p->lengthBytes[1] = p->length;
   p->pos = 0;
   p->state = ReceiveState::None;
   memcpy(p->buffer, payload, length);
   if (!queue_try_add(&outputQueue, &p)) {
      delete p;
      return false;
   }
   return true;
}



void Init(uint sdaPin, uint sclPin, uint addr, uint baudrate) {
   // Configure pins and I2C.
   gpio_init(sdaPin);
   gpio_set_function(sdaPin, GPIO_FUNC_I2C);
   gpio_pull_up(sdaPin);
   // Maxing the drive strength seem to help the host connect more reliably
   // Maxing out client side to the max sink strength to mirror the host 
   gpio_set_drive_strength(sdaPin, GPIO_DRIVE_STRENGTH_12MA);

   gpio_init(sclPin);
   gpio_set_function(sclPin, GPIO_FUNC_I2C);
   gpio_pull_up(sclPin);
   gpio_set_drive_strength(sclPin, GPIO_DRIVE_STRENGTH_12MA);

   i2c_init(i2c0, baudrate);
   i2c_slave_init(i2c0, addr, &i2c_slave_handler);

   // Initialize data structures for synchronizing between I2C interrupt and the main process.
   //
   // Use explicitly claimed spinlocks (from the SDK's reserved "claim free" range) instead of
   // queue_init()'s default striped-pool allocation. The striped pool only has 8 IDs and is
   // shared, via round-robin, with everything else in the program that also uses it -
   // including multicore_lockout's internal mutex, which is lazily initialized on its first
   // use (during an I2C firmware upgrade's flash erase/program). If that mutex happens to land
   // on the same physical spinlock number as one of these queues, the queue's synchronization
   // gets silently corrupted the moment a lockout is used, and it never recovers on its own.
   int outputSpinlock = spin_lock_claim_unused(true);
   int inputSpinlock = spin_lock_claim_unused(true);
   int availSpinlock = spin_lock_claim_unused(true);
   queue_init_with_spinlock(&outputQueue, sizeof(Packet*), 20, outputSpinlock);
   queue_init_with_spinlock(&inputQueue, sizeof(zuluide::i2c::client::Packet*), INPUT_BUFFER_COUNT, inputSpinlock);
   queue_init_with_spinlock(&availInputQueue, sizeof(zuluide::i2c::client::Packet*), INPUT_BUFFER_COUNT, availSpinlock);

   for (int i = 0; i < INPUT_BUFFER_COUNT; i++) {
      auto p = new Packet();
      Cleanup(p);
   }
}

void Cleanup(Packet* packet) {
   // Cleanup buffer and put back into service.
   packet->length = 0;
   packet->pos = 0;
   packet->state = ReceiveState::None;
   memset(packet->buffer, 0, MAX_MSG_SIZE);
   queue_try_add(&availInputQueue, &packet);
}

bool Is(Packet* toCheck, uint8_t messageID) {
   return toCheck->command == messageID;
}

bool TryReceive(Packet** toRecv) {
   return queue_try_remove(&inputQueue, toRecv);
}

void ProcessMessages() {
   zuluide::i2c::client::Packet* toRecv;
   if (TryReceive(&toRecv)) {
      if (Is(toRecv, I2C_SERVER_API_VERSION)) {
         ProcessServerAPIVersion(toRecv->buffer, toRecv->length);
      } else if (Is(toRecv, I2C_SERVER_WIFI_CONNECT)) {
         ProcessWiFiConnect();
      } else if (Is(toRecv, I2C_SERVER_SYSTEM_STATUS_JSON)) {
         ProcessSystemStatus(toRecv->buffer, toRecv->length);
      } else if (Is(toRecv, I2C_SERVER_UPDATE_FILENAME_CACHE)) {
         ProcessUpdateFilenames(toRecv->buffer, toRecv->length);
      } else if (Is(toRecv, I2C_SERVER_IMAGE_FILENAME)) {
         ProcessFilename(toRecv->buffer, toRecv->length);
      } else if (Is(toRecv, I2C_SERVER_IMAGE_JSON)) {
         ProcessImage(toRecv->buffer, toRecv->length);
      } else if (Is(toRecv, I2C_SERVER_SSID)) {
         ProcessSSID(toRecv->buffer, toRecv->length);
      } else if (Is(toRecv, I2C_SERVER_SSID_PASS)) {
         ProcessPassword(toRecv->buffer, toRecv->length);
      } else if (Is(toRecv, I2C_SERVER_RESET)) {
         ProcessReset();
      } else if (Is(toRecv, I2C_SERVER_STATIC_IP)) {
         ProcessStaticIP(toRecv->buffer, toRecv->length);
      } else if (Is(toRecv, I2C_SERVER_IP_ADDRESS_ACK)) {
         ProcessIPAddressAck();
      } else if (Is(toRecv, I2C_SERVER_DEVICE_LIST_JSON)) {
         ProcessDeviceList(toRecv->buffer, toRecv->length);
      } else if (Is(toRecv, I2C_SERVER_SD_STATUS_CHANGE)) {
         ProcessSDStatus(toRecv->buffer, toRecv->length);
      } else if (Is(toRecv, I2C_SERVER_UPDATE_FW_REQUEST)) {
         ProcessUpgradeFirmwareRequest(toRecv->buffer, toRecv->length);
      } else if (Is(toRecv, I2C_SERVER_UPDATE_FW_DATA)) {
         ProcessUpgradeFirmwareData(toRecv->buffer, toRecv->length);
      } else {
         printf("Unknown message received: %02X\n", toRecv->command);
      }


      // Cleanup buffer and put back into service.
      Cleanup(toRecv);
   }
}
}  // namespace zuluide::i2c::client

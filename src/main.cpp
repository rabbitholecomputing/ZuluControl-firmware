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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 **/

#include <hardware/watchdog.h>
#include <pico/i2c_slave.h>
#include <pico/stdlib.h>
#include <pico/util/queue.h>
#include <pico/multicore.h>


#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>

#include "ZuluControlI2CClient.h"
#include "index_html.h"
#include "fw_upgrade.h"
#include "lwip/apps/fs.h"
#include "lwip/apps/httpd.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/opt.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include "pico/cyw43_arch.h"
#include "url_decode.h"

static const uint I2C_SLAVE_ADDRESS = 0x45;
static const uint I2C_BAUDRATE = 400000;  // 400 kHz (Fast Mode)

static const uint I2C_SLAVE_SDA_PIN = 0;
static const uint I2C_SLAVE_SCL_PIN = 1;

static const uint8_t GPIO_BOARD_TYPE = 5;

extern const uint8_t GPIO_MCU_LED = 26;

static const uint8_t MAX_SCSI_IDS = 16;

static bool g_board_type_b = false;

// ── Device type / SD status ───────────────────────────────────────────────────

enum class DeviceType { Unknown, ZuluIDE, ZuluSCSI };
static DeviceType g_device_type = DeviceType::Unknown;
static bool g_sd_present = false;

// ── Filename cache ────────────────────────────────────────────────────────────

enum class FilenameCacheState { Idle, Start, Fetching, Full, Overflow };

// Single backing buffer for all filename JSON strings.
static char filenames_json[FILENAMES_JSON_CACHE_SIZE] = {0};
// Per-SCSI-ID pointers into filenames_json; NULL means that ID is not cached.
// ZuluIDE always uses index 0.  All-or-nothing: if g_filenames_overflow is true
// the cache is invalid for every ID regardless of whether its pointer is set.
static char *g_filenames_scsi_id[MAX_SCSI_IDS] = {nullptr};
// Next free byte in filenames_json for the current write pass.
static char *g_filenames_write_ptr = filenames_json;
// Start of the JSON segment being built for g_filenames_active_id.
static char *g_filenames_id_start = nullptr;
// True if any ID overflowed the buffer; the entire cache is a miss when set.
static bool  g_filenames_overflow = false;
// SCSI ID currently being received (0xFF = none in progress).
static uint8_t g_filenames_active_id = 0xFF;
// SCSI ID whose pointer to serve for the next /filenames.json response.
static uint8_t g_filenames_serving_id = 0xFF;

static volatile FilenameCacheState filenameState = FilenameCacheState::Idle;

// ── Image/iterator cache ──────────────────────────────────────────────────────

enum class ImageCacheState { Idle, Fetching, Full, Iterating, IteratingFinished };

static volatile ImageCacheState imageState = ImageCacheState::Idle;
static queue_t imageQueue;
static std::vector<char *> images;
static char *imageJson = NULL;

// ── Device list (ZuluSCSI) ────────────────────────────────────────────────────

static char deviceListJson[MAX_MSG_SIZE] = {0};

// ── IP / WiFi ─────────────────────────────────────────────────────────────────

enum class IPAddressState { Init, Sending, Received };
static volatile IPAddressState ipAddrState = IPAddressState::Init;

bool static_ip_set = false;
static ip4_addr_t static_ip, static_gw, static_netmask;
char ipBuffer[32] = {0};

// ── Status / version JSON buffers ─────────────────────────────────────────────

static char versionJson[MAX_MSG_SIZE];
static char currentStatus[MAX_MSG_SIZE];

// ── WiFi credentials ──────────────────────────────────────────────────────────

static std::string wifiPass;
static bool wifiPassSet = false;
static std::string wifiSSID;
static std::string serverAPIVersion;

// ── State machine ─────────────────────────────────────────────────────────────

enum class State {
    Unknown,
    WaitForAPIVersion,
    WaitingForSSID,
    WaitingForPassword,
    WaitingForConnect,
    WIFIInit,
    WIFIDown,
    FirmwareUpgradeI2C,
    Normal
};

namespace ClientMessage {
    namespace Prefix {
        constexpr char Normal  = 'n';
        constexpr char Debug   = 'd';
        constexpr char Unknown = 'u';
    }
    enum class Type { Normal, Debug };
}

static State programState = State::WaitForAPIVersion;

void RebuildImageJson();

static uint32_t millis() {
    return to_ms_since_boot(get_absolute_time());
}

static void reset() {
    zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_RESET_QUEUE);
    static_ip_set = false;
    memset(&static_ip, 0, sizeof(static_ip));
    memset(&static_netmask, 0, sizeof(static_netmask));
    memset(&static_gw, 0, sizeof(static_gw));
}

// ── I2C callback implementations ─────────────────────────────────────────────

namespace zuluide::i2c::client {

void LogMessageToServer(ClientMessage::Type type, const char* format, ...)
{
    char prefix = ClientMessage::Prefix::Unknown;
    switch (type) {
        case ClientMessage::Type::Normal: prefix = ClientMessage::Prefix::Normal; break;
        case ClientMessage::Type::Debug:  prefix = ClientMessage::Prefix::Debug;  break;
        default: break;
    }
    size_t format_len = strlen(format);
    if (format_len > MAX_MSG_SIZE - 2) format_len = MAX_MSG_SIZE - 2;
    char *message = new char[format_len + 2];
    memset(message, '\0', format_len + 2);
    message[0] = prefix;
    strncpy(message + 1, format, format_len);

    char payload[MAX_MSG_SIZE] = {0};
    va_list args;
    va_start(args, format);
    vsnprintf(payload, sizeof(payload), message, args);
    va_end(args);
    printf("%s\n", payload + 1);
    zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_LOG_MSG, payload);
    delete[] message;
}

void ProcessServerAPIVersion(const uint8_t *message, size_t length)
{
    memset(versionJson, '\0', sizeof(versionJson));
    strcat(versionJson, "{\"clientAPIVersion\":\"");
    strcat(versionJson, I2C_API_VERSION);
    strcat(versionJson, "\"");
    printf("Client API version: v%s\n", I2C_API_VERSION);

    strcat(versionJson, ", \"clientFWVersion\":\"");
    strcat(versionJson, FW_VERSION);
    strcat(versionJson, "\"");

    bool matching_major_version = false;
    unsigned long server_major_version = 0;
    unsigned long client_major_version = 0;
    char* period_location = strchr(I2C_API_VERSION, '.');
    client_major_version = strtoul(I2C_API_VERSION, &period_location, 10);

    // Detect device type from version string: "4.0.0 ZuluSCSI" or "4.0.0 ZuluIDE"
    const char *device_name = "ZuluIDE";  // default for backwards compatibility
    if (length > 0) {
        serverAPIVersion = std::string((const char*)message, length);
        strcat(versionJson, ", \"serverAPIVersion\":\"");
        strncat(versionJson, (const char*)message, length < MAX_MSG_SIZE - 100 ? length : MAX_MSG_SIZE - 100);
        strcat(versionJson, "\"");
        printf("Server API version: v%s\n", serverAPIVersion.c_str());

        // Parse major version (strtoul stops at non-digit, space, or dot)
        period_location = strchr((const char*)message, '.');
        if (period_location != NULL) {
            server_major_version = strtoul((const char*)message, &period_location, 10);
            if (server_major_version > 0 && server_major_version == client_major_version) {
                matching_major_version = true;
            }
        }

        // Parse device name from after the first space
        const char *space = (const char *)memchr(message, ' ', length);
        if (space != NULL && (space + 1) < ((const char*)message + length)) {
            const char *parsed_name = space + 1;
            size_t name_len = length - (size_t)(parsed_name - (const char*)message);
            if (name_len >= 7 && strncmp(parsed_name, "ZuluSCSI", 8) == 0) {
                device_name = "ZuluSCSI";
                g_device_type = DeviceType::ZuluSCSI;
                printf("Detected device: ZuluSCSI\n");
            } else if (name_len >= 7 && strncmp(parsed_name, "ZuluIDE", 7) == 0) {
                device_name = "ZuluIDE";
                g_device_type = DeviceType::ZuluIDE;
                printf("Detected device: ZuluIDE\n");
            }
        } else {
            // No device name - old firmware, assume ZuluIDE
            g_device_type = DeviceType::ZuluIDE;
            printf("No device name in version string, assuming ZuluIDE\n");
        }
    } else {
        strcat(versionJson, ", \"serverAPIVersion\":\"Unknown\"");
        printf("Error: no API version received from server\n");
        g_device_type = DeviceType::ZuluIDE;
    }

    strcat(versionJson, ", \"deviceType\":\"");
    strcat(versionJson, device_name);
    strcat(versionJson, "\"");

    if (!matching_major_version) {
        strcat(versionJson, ", \"message\":\"API major version mismatch. Please update both devices to the latest firmware. "
            "<br/> <a href='https://github.com/ZuluIDE/ZuluIDE-HTTP-PicoW/releases'>ZuluControl firmware</a>\"");
        printf("Warning: major versions between client and server do not match. Please upgrade both devices.\n");
    }

    strcat(versionJson, "}");
    EnqueueRequest(I2C_CLIENT_RESET_QUEUE);
    if (g_device_type == DeviceType::ZuluSCSI) {
        EnqueueRequest(I2C_CLIENT_API_VERSION, I2C_API_VERSION);
    }
    programState = State::WaitingForSSID;
}

void ProcessWiFiConnect()
{
    printf("Wifi Connect Received\n");
    programState = State::WIFIInit;
}

void ProcessSystemStatus(const uint8_t *message, size_t length)
{
    memset(currentStatus, 0, MAX_MSG_SIZE);
    memcpy(currentStatus, message, length < MAX_MSG_SIZE ? length : MAX_MSG_SIZE - 1);
}

void ProcessUpdateFilenames(const uint8_t *message, size_t length)
{
    uint8_t incoming_id = 0;  // ZuluIDE always uses slot 0
    if (g_device_type == DeviceType::ZuluSCSI) {
        incoming_id = (length > 0) ? message[0] : 0xFF;
        if (incoming_id >= MAX_SCSI_IDS) {
            printf("Ignoring filename cache update for invalid SCSI ID %u\n", incoming_id);
            return;
        }
    }

    // Reset the entire buffer only after an overflow.  A cached-ID update (e.g.
    // a FETCH response arriving after the subscribe push already populated the
    // cache) must NOT reset other IDs' pointers; new data is simply appended
    // after the current write pointer and the pointer for incoming_id is
    // updated.  This prevents a narrow race where a browser request between two
    // consecutive subscribe-push segments triggers a FETCH that later resets
    // the whole buffer.
    if (g_filenames_overflow) {
        printf("Resetting filename buffer after overflow\n");
        memset(filenames_json, 0, sizeof(filenames_json));
        for (int i = 0; i < MAX_SCSI_IDS; i++) g_filenames_scsi_id[i] = nullptr;
        g_filenames_write_ptr = filenames_json;
        g_filenames_overflow = false;
    }

    printf("Beginning filename cache update for %s ID %u\n",
           g_device_type == DeviceType::ZuluSCSI ? "SCSI" : "IDE", incoming_id);
    g_filenames_active_id = incoming_id;
    g_filenames_id_start   = g_filenames_write_ptr;
    filenameState = FilenameCacheState::Start;
}

void ProcessFilename(const uint8_t *message, size_t length)
{
    const uint8_t *data = message;
    size_t data_len = length;

    if (g_device_type == DeviceType::ZuluSCSI) {
        if (length == 0) {
            // fall through - end sentinel for the active ID
        } else {
            uint8_t incoming_id = message[0];
            if (incoming_id != g_filenames_active_id) return;
            data = message + 1;
            data_len = length - 1;
        }
    }

    if (g_filenames_active_id >= MAX_SCSI_IDS) return;
    // Discard incoming data once an overflow has been flagged; the next
    // ProcessUpdateFilenames call will reset the buffer.
    if (g_filenames_overflow) return;

    char *buf_end = filenames_json + FILENAMES_JSON_CACHE_SIZE;
    printf("Process filename length: %zu\n", data_len);

    // Write the opening JSON header on the first call for this ID.
    if (filenameState == FilenameCacheState::Start) {
        const char *hdr = "{\"filenames\":[";
        size_t hdr_len = strlen(hdr);
        // Reserve space for the minimum closing ]}NUL as well.
        if (g_filenames_write_ptr + hdr_len + 3 > buf_end) {
            printf("Filename cache overflowed after init\n");
            g_filenames_overflow = true;
            filenameState = FilenameCacheState::Overflow;
            return;
        }
        memcpy(g_filenames_write_ptr, hdr, hdr_len);
        g_filenames_write_ptr += hdr_len;
    }

    if (data_len > 0) {
        bool first = (filenameState == FilenameCacheState::Start);
        // Space needed: optional comma, opening quote, data, closing quote, then ]}NUL at minimum.
        size_t needed = (first ? 1 : 2) + data_len + 1 + 3;
        if (g_filenames_write_ptr + needed > buf_end) {
            printf("Filename cache overflowed\n");
            g_filenames_overflow = true;
            filenameState = FilenameCacheState::Overflow;
            return;
        }
        if (!first) *g_filenames_write_ptr++ = ',';
        *g_filenames_write_ptr++ = '"';
        memcpy(g_filenames_write_ptr, data, data_len);
        g_filenames_write_ptr += data_len;
        *g_filenames_write_ptr++ = '"';
        filenameState = FilenameCacheState::Fetching;
    } else {
        // End of list sentinel
        if (filenameState == FilenameCacheState::Start || filenameState == FilenameCacheState::Fetching) {
            if (g_filenames_write_ptr + 3 > buf_end) {
                printf("Filename cache overflowed at closing\n");
                g_filenames_overflow = true;
                filenameState = FilenameCacheState::Overflow;
                return;
            }
            *g_filenames_write_ptr++ = ']';
            *g_filenames_write_ptr++ = '}';
            *g_filenames_write_ptr++ = '\0';

            g_filenames_scsi_id[g_filenames_active_id] = g_filenames_id_start;

            if (g_device_type == DeviceType::ZuluSCSI) {
                printf("Cached filenames for SCSI ID %u\n", g_filenames_active_id);
                g_filenames_active_id = 0xFF;
                filenameState = FilenameCacheState::Idle;
            } else {
                printf("Received filename of length zero, setting state to Full\n");
                filenameState = FilenameCacheState::Full;
            }
        }
    }
}

void ProcessImage(const uint8_t *message, size_t length)
{
    const uint8_t *data = message;
    size_t data_len = length;

    if (g_device_type == DeviceType::ZuluSCSI && length > 0) {
        // ZuluSCSI payload: [scsi_id][json...]; end sentinel is just [scsi_id] (data_len==0)
        data = message + 1;
        data_len = length - 1;
    }

    if (data_len > 0) {
        char *image = new char[data_len + 1];
        memset(image, 0, data_len + 1);
        memcpy(image, data, data_len);
        if (imageState == ImageCacheState::Iterating) {
            queue_try_add(&imageQueue, &image);
        } else {
            images.push_back(image);
        }
    } else {
        if (imageState == ImageCacheState::Iterating) {
            imageState = ImageCacheState::IteratingFinished;
        } else {
            RebuildImageJson();
            imageState = ImageCacheState::Full;
        }
    }
}

void ProcessSSID(const uint8_t *message, size_t length)
{
    if (length > 0) {
        wifiSSID = std::string((const char *)message, length);
        printf("Using WIFI SSID (%s) from the server.\n", wifiSSID.c_str());
    } else if (strlen(WIFI_SSID) > 0) {
        wifiSSID = std::string(WIFI_SSID);
        printf("Using WIFI SSID (%s) compiled into the application.\n", wifiSSID.c_str());
    } else {
        printf("No WIFI SSID retrieved from server and none compiled into the application.\n");
        return;
    }
    EnqueueRequest(I2C_CLIENT_RESET_QUEUE);
    programState = State::WaitingForPassword;
}

void ProcessPassword(const uint8_t *message, size_t length)
{
    if (length > 0) {
        printf("Using WIFI password from the server.\n");
    } else {
        printf("WiFi password cleared, assuming open WiFi network.\n");
    }
    wifiPass = std::string((const char *)message, length);
    wifiPassSet = true;
    EnqueueRequest(I2C_CLIENT_RESET_QUEUE);
    programState = State::WaitingForConnect;
}

void ProcessReset()
{
    printf("Reset Received.\n");
    reset();
    programState = State::WaitForAPIVersion;
}

void ProcessStaticIP(const uint8_t* message, size_t length)
{
    if (length <= 3) {
        static_ip_set = false;
    } else {
        static_ip_set = false;
        const char *ip_data = (const char*) &message[2];
        if (strncmp("ip", (const char*)message, 2) == 0) {
            printf("Setting static IP to %s\n", ip_data);
            static_ip_set = ip4addr_aton(ip_data, &static_ip);
        } else if (strncmp("nm", (const char*)message, 2) == 0) {
            printf("Setting static IP netmask to %s\n", ip_data);
            static_ip_set = ip4addr_aton(ip_data, &static_netmask);
        } else if (strncmp("gw", (const char*)message, 2) == 0) {
            printf("Setting static IP gateway to %s\n", ip_data);
            static_ip_set = ip4addr_aton(ip_data, &static_gw);
        }
    }
}

void ProcessIPAddressAck()
{
    ipAddrState = IPAddressState::Received;
    printf("Server received IP Address.\n");
}

void ProcessDeviceList(const uint8_t *message, size_t length)
{
    memset(deviceListJson, 0, sizeof(deviceListJson));
    size_t copy_len = length < sizeof(deviceListJson) - 1 ? length : sizeof(deviceListJson) - 1;
    memcpy(deviceListJson, message, copy_len);
    printf("Received device list JSON (%zu bytes)\n", length);
}

void ProcessSDStatus(const uint8_t *message, size_t length)
{
    if (length < 1) return;
    g_sd_present = (message[0] == I2C_SERVER_SD_PRESENT);
    printf("SD card %s\n", g_sd_present ? "present" : "not present");
}

void ProcessUpgradeFirmwareRequest(const uint8_t* message, size_t length) {
   if (length != 1) {
      printf("Invalid firmware upgrade request received.\n");
      return;
   }

   uint8_t requestType = message[0];
   switch (requestType) {
      case I2C_SERVER_FW_UPGRADE_START:
         printf("Firmware upgrade request received: START\n");
         fwupgrade_i2c_begin();
         programState = State::FirmwareUpgradeI2C;
         break;
      case I2C_SERVER_FW_UPGRADE_FINISH:
         printf("Firmware upgrade request received: FINISH\n");
         fwupgrade_i2c_finished();
         programState = State::WaitForAPIVersion;
         break;
      case I2C_SERVER_FW_UPGRADE_ABORT:
         printf("Firmware upgrade request received: ABORT\n");
         fwupgrade_i2c_begin();
         programState = State::WaitForAPIVersion;
         break;
      case I2C_SERVER_FW_UPGRADE_RETRY:
         // Host detected a CRC/length mismatch on the chunk it just acked --
         // discard the staged data (it will be resent) without committing it.
         printf("Firmware upgrade request received: RETRY\n");
         fwupgrade_i2c_discard_staged();
         break;
      default:
         printf("Unknown firmware upgrade request received: %02X\n", requestType);
         break;
   }
}

// Table-driven software CRC32, reflected CRC-32/ISO-HDLC (aka the
// zlib/gzip/PNG/Ethernet CRC32), polynomial 0xEDB88320 (the bit-reversed
// form of 0x04C11DB7), seed 0xFFFFFFFF, final XOR 0xFFFFFFFF -- so the
// client computes its own CRC in software as it receives each chunk over
// the ordinary byte-ISR path.
static uint32_t g_crc32_table[256];
static bool g_crc32_table_ready = false;

static void Crc32TableInit() {
   for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++) {
         c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      }
      g_crc32_table[i] = c;
   }
   g_crc32_table_ready = true;
}

static uint32_t Crc32(const uint8_t* data, size_t length) {
   if (!g_crc32_table_ready) Crc32TableInit();
   uint32_t crc = 0xFFFFFFFFu;
   for (size_t i = 0; i < length; i++) {
      crc = g_crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
   }
   return crc ^ 0xFFFFFFFFu;
}

void ProcessUpgradeFirmwareData(const uint8_t* message, size_t length) {
   // Implicit confirmation: receiving a *new* chunk means the host got a
   // matching-CRC ack for the *previous* one and is proceeding, so it's now
   // safe to commit (flash) the previously staged chunk.
   fwupgrade_i2c_commit_staged();

   uint32_t crc = Crc32(message, length);
   fwupgrade_i2c_stage_chunk(message, length);

   uint8_t ackPayload[6];
   ackPayload[0] = (length >> 8) & 0xFF;
   ackPayload[1] = length & 0xFF;
   ackPayload[2] = (crc >> 24) & 0xFF;
   ackPayload[3] = (crc >> 16) & 0xFF;
   ackPayload[4] = (crc >> 8) & 0xFF;
   ackPayload[5] = crc & 0xFF;
   EnqueueRequestBinary(I2C_CLIENT_UPDATE_FW_ACK, ackPayload, sizeof(ackPayload));
}

}  // namespace zuluide::i2c::client

// ── CGI handlers ─────────────────────────────────────────────────────────────

static const char *cgi_handler_version(int index, int numParams, char *pcParam[], char *pcValue[]) {
    return "/version.json";
}

static const char *cgi_handler_status(int index, int numParams, char *pcParam[], char *pcValue[]) {
    return "/status.json";
}

static const char *cgi_handler_filenames(int index, int numParams, char *pcParam[], char *pcValue[]) {
    printf("Filenames CGI requested\n");

    if (g_device_type == DeviceType::ZuluSCSI) {
        uint8_t req_id = 0;
        for (int i = 0; i < numParams; i++) {
            if (strncmp(pcParam[i], "scsiId", 7) == 0) {
                req_id = (uint8_t)atoi(pcValue[i]);
            }
        }
        if (req_id >= MAX_SCSI_IDS) req_id = 0;

        // Cache hit: this ID has a valid pointer and no overflow has occurred.
        // The cache is used only when all responses that were pushed by ZuluSCSI
        // fit in the single buffer (overflow flag stays clear).
        if (g_filenames_scsi_id[req_id] != nullptr && !g_filenames_overflow) {
            printf("Serving cached filenames for SCSI ID %u\n", req_id);
            g_filenames_serving_id = req_id;
            return "/filenames.json";
        }

        // A fetch is already in progress for this exact ID - wait for it.
        if (g_filenames_active_id == req_id) {
            return "/wait.json";
        }

        // Cache miss with no active fetch: request from ZuluSCSI.
        if (filenameState == FilenameCacheState::Idle ||
            filenameState == FilenameCacheState::Overflow) {
            printf("Requesting filenames for SCSI ID %u from ZuluSCSI\n", req_id);
            g_filenames_active_id = req_id;
            g_filenames_serving_id = req_id;
            uint8_t payload[1] = {req_id};
            if (!zuluide::i2c::client::EnqueueRequestBinary(I2C_CLIENT_FETCH_FILENAMES, payload, 1)) {
                printf("Failed to add fetch filenames to output queue.\n");
            }
        }
        return "/wait.json";

    } else {
        // ZuluIDE: single-device; serve from cache (slot 0) and trigger background refresh.
        g_filenames_active_id = 0;
        g_filenames_serving_id = 0;
        if (filenameState == FilenameCacheState::Full) {
            if (!zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_FETCH_FILENAMES)) {
                printf("Failed to add fetch filenames to output queue.\n");
            }
        } else if (filenameState == FilenameCacheState::Idle) {
            if (!zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_FETCH_FILENAMES)) {
                printf("Failed to add fetch filenames to output queue.\n");
            }
            filenameState = FilenameCacheState::Fetching;
        }
    }

    if (filenameState == FilenameCacheState::Start || filenameState == FilenameCacheState::Fetching) {
        return "/wait.json";
    }
    if (filenameState == FilenameCacheState::Overflow) {
        return "/overflow.json";
    }
    return "/filenames.json";
}

static const char *cgi_handler_imgs(int index, int numParams, char *pcParam[], char *pcValue[]) {
    if (g_device_type == DeviceType::ZuluSCSI) {
        uint8_t scsi_id = 0;
        for (int i = 0; i < numParams; i++) {
            if (strncmp(pcParam[i], "scsiId", 7) == 0) {
                scsi_id = (uint8_t)atoi(pcValue[i]);
            }
        }
        if (imageState == ImageCacheState::Idle) {
            imageState = ImageCacheState::Fetching;
            uint8_t payload[1] = {scsi_id};
            if (!zuluide::i2c::client::EnqueueRequestBinary(I2C_CLIENT_FETCH_IMAGES_JSON, payload, 1)) {
                printf("Failed to add fetch images to output queue.\n");
            }
        }
    } else {
        if (imageState == ImageCacheState::Idle) {
            imageState = ImageCacheState::Fetching;
            if (!zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_FETCH_IMAGES_JSON)) {
                printf("Failed to add fetch images to output queue.\n");
            }
        }
    }

    if (imageState == ImageCacheState::Fetching) {
        return "/wait.json";
    }
    return "/images.json";
}

static const char *cgi_handler_next_image(int index, int numParams, char *pcParam[], char *pcValue[]) {
    uint8_t scsi_id = 0;
    for (int i = 0; i < numParams; i++) {
        if (strncmp(pcParam[i], "scsiId", 7) == 0) {
            scsi_id = (uint8_t)atoi(pcValue[i]);
        }
    }

    if (imageState == ImageCacheState::Idle) {
        if (g_device_type == DeviceType::ZuluSCSI) {
            uint8_t payload[1] = {scsi_id};
            if (!zuluide::i2c::client::EnqueueRequestBinary(I2C_CLIENT_FETCH_ITR_IMAGE, payload, 1)) {
                printf("Failed to add iterate image to output queue.\n");
            }
        } else {
            if (!zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_FETCH_ITR_IMAGE)) {
                printf("Failed to add iterate image to output queue.\n");
            }
        }
        imageState = ImageCacheState::Iterating;
        return "/wait.json";
    } else if (imageState == ImageCacheState::Iterating) {
        if (queue_is_empty(&imageQueue)) {
            return "/wait.json";
        } else {
            if (g_device_type == DeviceType::ZuluSCSI) {
                uint8_t payload[1] = {scsi_id};
                if (!zuluide::i2c::client::EnqueueRequestBinary(I2C_CLIENT_FETCH_ITR_IMAGE, payload, 1)) {
                    printf("Failed to add iterate image to output queue.\n");
                }
            } else {
                if (!zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_FETCH_ITR_IMAGE)) {
                    printf("Failed to add iterate image to output queue.\n");
                }
            }
        }
    } else if (imageState == ImageCacheState::IteratingFinished) {
        imageState = ImageCacheState::Idle;
        return "/done.json";
    }
    return "/nextImage.json";
}

static const char *cgi_handler_image(int index, int numParams, char *params[], char *values[]) {
    uint8_t scsi_id = 0;
    const char *image_name = NULL;

    for (int i = 0; i < numParams; i++) {
        if (strncmp(params[i], "imageName", 10) == 0) {
            urldecode(values[i]);
            image_name = values[i];
        } else if (strncmp(params[i], "scsiId", 7) == 0) {
            scsi_id = (uint8_t)atoi(values[i]);
        }
    }

    if (image_name == NULL) return "/error.json";

    if (g_device_type == DeviceType::ZuluSCSI) {
        // Prepend scsi_id byte before the path
        size_t path_len = strlen(image_name);
        uint8_t *payload = new uint8_t[1 + path_len];
        payload[0] = scsi_id;
        memcpy(payload + 1, image_name, path_len);
        printf("ZuluSCSI loading image \"%s\" on SCSI ID %u\n", image_name, scsi_id);
        zuluide::i2c::client::EnqueueRequestBinary(I2C_CLIENT_LOAD_IMAGE, payload, (uint16_t)(1 + path_len));
        delete[] payload;
    } else {
        printf("ZuluIDE setting image to: %s\n", image_name);
        zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_LOAD_IMAGE, image_name);
    }
    return "/ok.json";
}

static const char *cgi_handler_eject(int index, int numParams, char *params[], char *values[]) {
    if (g_device_type == DeviceType::ZuluSCSI) {
        uint8_t scsi_id = 0;
        for (int i = 0; i < numParams; i++) {
            if (strncmp(params[i], "scsiId", 7) == 0) {
                scsi_id = (uint8_t)atoi(values[i]);
            }
        }
        printf("ZuluSCSI ejecting SCSI ID %u\n", scsi_id);
        uint8_t payload[1] = {scsi_id};
        zuluide::i2c::client::EnqueueRequestBinary(I2C_CLIENT_EJECT_IMAGE, payload, 1);
    } else {
        zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_EJECT_IMAGE);
    }
    return "/ok.json";
}

static const char *cgi_handler_insert_media(int index, int numParams, char *params[], char *values[]) {
    uint8_t scsi_id = 0;
    for (int i = 0; i < numParams; i++) {
        if (strncmp(params[i], "scsiId", 7) == 0) {
            scsi_id = (uint8_t)atoi(values[i]);
        }
    }
    printf("ZuluSCSI inserting media on SCSI ID %u\n", scsi_id);
    uint8_t payload[1] = {scsi_id};
    zuluide::i2c::client::EnqueueRequestBinary(I2C_CLIENT_INSERT_MEDIA, payload, 1);
    return "/ok.json";
}

static const char *cgi_handler_device_list(int index, int numParams, char *params[], char *values[]) {
    zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_FETCH_DEVICE_LIST);
    return "/devicelist.json";
}

static const tCGI cgi_handlers[] = {
    {"/version",     cgi_handler_version},
    {"/status",      cgi_handler_status},
    {"/filenames",   cgi_handler_filenames},
    {"/images",      cgi_handler_imgs},
    {"/image",       cgi_handler_image},
    {"/eject",       cgi_handler_eject},
    {"/nextImage",   cgi_handler_next_image},
    {"/insertMedia", cgi_handler_insert_media},
    {"/deviceList",  cgi_handler_device_list},
};

// ── POST handlers (firmware upgrade) ─────────────────────────────────────────

err_t (*g_httpd_post_receive_data_handler)(void *connection, struct pbuf *p);
void (*g_httpd_post_finished_handler)(void *connection, char *response_uri, u16_t response_uri_len);

err_t httpd_post_begin(void *connection, const char *uri, const char *http_request,
                       u16_t http_request_len, int content_len, char *response_uri,
                       u16_t response_uri_len, u8_t *post_auto_wnd)
{
    if (strcmp(uri, "/fw_upgrade.cgi") == 0) {
        g_httpd_post_receive_data_handler = &fwupgrade_post_receive_data;
        g_httpd_post_finished_handler     = &fwupgrade_post_finished;
        return fwupgrade_post_begin(connection, uri, http_request, http_request_len, content_len,
                                    response_uri, response_uri_len, post_auto_wnd);
    }
    g_httpd_post_receive_data_handler = nullptr;
    g_httpd_post_finished_handler     = nullptr;
    return ERR_VAL;
}

err_t httpd_post_receive_data(void *connection, struct pbuf *p)
{
    err_t result = ERR_VAL;
    if (g_httpd_post_receive_data_handler)
        result = g_httpd_post_receive_data_handler(connection, p);
    return result;
}

void httpd_post_finished(void *connection, char *response_uri, u16_t response_uri_len)
{
    if (g_httpd_post_finished_handler)
        g_httpd_post_finished_handler(connection, response_uri, response_uri_len);
    g_httpd_post_receive_data_handler = nullptr;
    g_httpd_post_finished_handler     = nullptr;
}

// ── Core 1 (I2C slave) ────────────────────────────────────────────────────────

void core1_main() {
    // Lets core0 safely park us in RAM (via multicore_lockout_start_blocking()) while it
    // erases/programs flash during an I2C firmware upgrade, instead of us stalling/hanging
    // trying to fetch code from the flash core0 currently has locked for the erase/program.
    multicore_lockout_victim_init();
    zuluide::i2c::client::Init(I2C_SLAVE_SDA_PIN, I2C_SLAVE_SCL_PIN, I2C_SLAVE_ADDRESS, I2C_BAUDRATE);
    multicore_fifo_push_blocking(0xbeef);
    while (true) { tight_loop_contents(); }
}

static bool has_elapsed(uint32_t start, uint32_t elapsed) {
    return (uint32_t)(millis() - start) > elapsed;
}

void start_multicore_i2c() {
    multicore_launch_core1(core1_main);
    uint32_t g = multicore_fifo_pop_blocking();
    if (g == 0xbeef) printf("Core 1 successfully launched");
    else              printf("Core 1 failed to launch");
}

using zuluide::i2c::client::LogMessageToServer;

// ── Main ──────────────────────────────────────────────────────────────────────

int main() {
    gpio_set_pulls(GPIO_BOARD_TYPE, true, false);
    busy_wait_us(1);
    g_board_type_b = !gpio_get(GPIO_BOARD_TYPE);
    if (g_board_type_b) {
        gpio_set_function(GPIO_MCU_LED, GPIO_FUNC_SIO);
        gpio_set_pulls(GPIO_MCU_LED, false, false);
        gpio_put(GPIO_MCU_LED, true);
        gpio_set_drive_strength(GPIO_MCU_LED, GPIO_DRIVE_STRENGTH_12MA);
        gpio_set_slew_rate(GPIO_MCU_LED, GPIO_SLEW_RATE_SLOW);
        gpio_set_dir(GPIO_MCU_LED, true);
    }

    stdio_init_all();
    printf("Starting.\n");

    memset(currentStatus, 0, MAX_MSG_SIZE);
    memset(versionJson, '\0', MAX_MSG_SIZE);
    memset(deviceListJson, '\0', MAX_MSG_SIZE);
    sprintf(versionJson, "{\"clientAPIVersion\":\"%s\", \"serverAPIVersion\": \"server failed to send version\", \"deviceType\":\"Unknown\"}",
            I2C_API_VERSION);
    queue_init(&imageQueue, sizeof(char *), 1);

    start_multicore_i2c();

    if (cyw43_arch_init()) {
        LogMessageToServer(ClientMessage::Type::Normal, "Failed to initialize WiFi interface. WiFi client halting.");
        return 1;
    }

    bool httpInitialized = false;
    bool started_blink = true;
    bool blink_on = false;
    uint32_t start_time = millis();
    uint32_t send_ip_start_time = millis();
    int number_of_blinks = 3;
    uint32_t waiting_start = millis();
    State last_state = State::Unknown;

    while (true) {
        // Startup blink
        if (started_blink) {
            if ((uint32_t)(millis() - start_time) > 500) {
                blink_on = !blink_on;
                gpio_put(GPIO_MCU_LED, blink_on);
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, blink_on);
                start_time = millis();
                if (!blink_on && --number_of_blinks <= 0) {
                    started_blink = false;
                }
            }
        }

        switch (programState) {
            case State::WaitForAPIVersion:
                if (programState != last_state || has_elapsed(waiting_start, I2C_CMD_RETRY_MS)) {
                    zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_API_VERSION, I2C_API_VERSION);
                    waiting_start = millis();
                }
                last_state = programState;
                zuluide::i2c::client::ProcessMessages();
                break;

            case State::WaitingForSSID:
                if (programState != last_state || has_elapsed(waiting_start, I2C_CMD_RETRY_MS)) {
                    printf("Waiting for SSID\n");
                    if (!zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_FETCH_SSID)) {
                        printf("Failed to add request for SSID to output queue\n");
                    }
                    waiting_start = millis();
                    printf("Waiting for SSID from server");
                }
                last_state = programState;
                zuluide::i2c::client::ProcessMessages();
                break;

            case State::WaitingForPassword:
                if (programState != last_state || has_elapsed(waiting_start, I2C_CMD_RETRY_MS)) {
                    printf("Waiting for Password\n");
                    if (!zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_FETCH_SSID_PASS)) {
                        printf("Failed to add request for Password to output queue\n");
                    }
                    waiting_start = millis();
                }
                last_state = programState;
                zuluide::i2c::client::ProcessMessages();
                break;

            case State::WaitingForConnect:
                if (programState != last_state) printf("Waiting for Connect\n");
                last_state = programState;
                zuluide::i2c::client::ProcessMessages();
                break;

            case State::WIFIInit: {
                last_state = programState;
                if (wifiSSID.empty()) {
                    programState = State::WaitingForSSID;
                    break;
                }
                printf("Initializing to WiFi.\n");
                started_blink = false;
                gpio_put(GPIO_MCU_LED, false);

                cyw43_arch_enable_sta_mode();
                cyw43_wifi_pm(&cyw43_state, cyw43_pm_value(CYW43_NO_POWERSAVE_MODE, 20, 1, 1, 1));

                if (static_ip_set) {
                    printf("Setting up static IP\n");
                    cyw43_arch_lwip_begin();
                    dhcp_stop(cyw43_state.netif);
                    netif_set_addr(cyw43_state.netif, &static_ip, &static_netmask, &static_gw);
                    cyw43_arch_lwip_end();
                } else {
                    printf("Setting up DHCP\n");
                    cyw43_arch_lwip_begin();
                    dhcp_start(cyw43_state.netif);
                    cyw43_arch_lwip_end();
                }
                programState = State::WIFIDown;
                break;
            }

            case State::WIFIDown: {
                last_state = programState;
                if (wifiSSID.empty()) {
                    cyw43_arch_lwip_begin();
                    dhcp_stop(cyw43_state.netif);
                    cyw43_arch_lwip_end();
                    reset();
                    programState = State::WaitingForSSID;
                } else {
                    bool open_network = (wifiPassSet && wifiPass.empty()) || (!wifiPassSet && sizeof(WIFI_PASSWORD) == 0);

                    if (open_network) {
                        LogMessageToServer(ClientMessage::Type::Normal, "Connecting to open WiFi network: %s", wifiSSID.c_str());
                    } else {
                        LogMessageToServer(ClientMessage::Type::Normal, "Connecting to secured WiFi network: %s", wifiSSID.c_str());
                    }

                    int connection_result;
                    if (open_network) {
                        connection_result = cyw43_arch_wifi_connect_timeout_ms(
                            wifiSSID.c_str(), nullptr, CYW43_AUTH_OPEN, WIFI_CONNECT_TIMEOUT_MS);
                    } else {
                        connection_result = cyw43_arch_wifi_connect_timeout_ms(
                            wifiSSID.c_str(), wifiPassSet ? wifiPass.c_str() : WIFI_PASSWORD,
                            CYW43_AUTH_WPA2_AES_PSK, WIFI_CONNECT_TIMEOUT_MS);
                    }

                    if (PICO_ERROR_NONE != connection_result) {
                        reset();
                        LogMessageToServer(ClientMessage::Type::Normal, "Failed to connect to WiFi.");
                        programState = State::WaitingForSSID;
                    } else {
                        zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_RESET_QUEUE);
                        LogMessageToServer(ClientMessage::Type::Normal, "Connected to WiFi.");
                        extern cyw43_t cyw43_state;
                        auto ip_addr = cyw43_state.netif[CYW43_ITF_STA].ip_addr.addr;

                        memset(ipBuffer, 0, 32);
                        sprintf(ipBuffer, "%lu.%lu.%lu.%lu",
                                ip_addr & 0xFF, (ip_addr >> 8) & 0xFF,
                                (ip_addr >> 16) & 0xFF, ip_addr >> 24);
                        printf("IP Address: %s\n", ipBuffer);
                        ipAddrState = IPAddressState::Sending;

                        if (!httpInitialized) {
                            httpd_init();
                            http_set_cgi_handlers(cgi_handlers, sizeof(cgi_handlers) / sizeof(cgi_handlers[0]));
                            LogMessageToServer(ClientMessage::Type::Debug, "Http server initialized.");
                            httpInitialized = true;
                        }

                        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

                        if (!zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_SUBSCRIBE_STATUS_JSON)) {
                            printf("Failed to add subscribe to output queue.\n");
                        }
                        programState = State::Normal;
                        LogMessageToServer(ClientMessage::Type::Debug, "System Ready");
                    }
                }
                break;
            }

            case State::Normal: {
                last_state = programState;
                zuluide::i2c::client::ProcessMessages();
                if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP) {
                    programState = State::WIFIDown;
                    LogMessageToServer(ClientMessage::Type::Normal, "WiFi connection down.\n");
                    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
                    gpio_put(GPIO_MCU_LED, false);
                    started_blink = false;
                }
                break;
            }

            case State::FirmwareUpgradeI2C: {
                last_state = programState;
                zuluide::i2c::client::ProcessMessages();
                break;
            }
            default:
                last_state = State::Unknown;
                printf("Error, unknown state.\n");
                break;
        }

        if ((uint32_t)(millis() - send_ip_start_time) > 3000) {
            if (IPAddressState::Sending == ipAddrState) {
                printf("Sending ip address %s\n", ipBuffer);
                zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_IP_ADDRESS, ipBuffer);
            }
            send_ip_start_time = millis();
        }
    }

    return 0;
}

// ── Image JSON rebuild ────────────────────────────────────────────────────────

void RebuildImageJson() {
    size_t totalSize = 2;
    for (auto item : images) {
        totalSize += strlen(item) + 1;
    }

    if (imageJson != NULL) {
        delete[] imageJson;
    }

    imageJson = new char[totalSize + 3];
    imageJson[0] = '[';
    int pos = 1;
    for (auto item : images) {
        if (pos > 1) {
            strcat(imageJson, ",");
            pos++;
        }
        strcat(imageJson, item);
        pos += strlen(item);
    }
    imageJson[pos] = ']';
    imageJson[pos + 1] = 0;

    for (auto item : images) { delete[] item; }
    images.clear();
}

// ── Custom file system (lwIP httpd) ──────────────────────────────────────────

int get_file_contents(struct fs_file *file, const char *fileContents, int fileLen) {
    memset(file, 0, sizeof(struct fs_file));
    if (fileContents) {
        file->pextension = (void*)fileContents;
        file->data  = NULL;
        file->len   = fileLen;
        file->index = 0;
        file->flags = FS_FILE_FLAGS_HEADER_PERSISTENT;
        return 1;
    }
    return 0;
}

int fs_open_custom(struct fs_file *file, const char *name) {
    printf("open custom name: %s\n", name);
    if (strncmp(name, "/status.json", sizeof("/status.json")) == 0) {
        // Inject sdPresent into the ZuluSCSI status JSON by stripping the trailing
        // '}' and appending the field before closing.
        static char statusBuf[MAX_MSG_SIZE + 32];
        size_t slen = strlen(currentStatus);
        if (slen > 0 && currentStatus[slen - 1] == '}') {
            snprintf(statusBuf, sizeof(statusBuf), "%.*s,\"sdPresent\":%s}",
                     (int)(slen - 1), currentStatus,
                     g_sd_present ? "true" : "false");
        } else {
            snprintf(statusBuf, sizeof(statusBuf), "{\"sdPresent\":%s}",
                     g_sd_present ? "true" : "false");
        }
        return get_file_contents(file, statusBuf, strlen(statusBuf));
    } else if (strncmp(name, "/images.json", sizeof("/images.json")) == 0) {
        return get_file_contents(file, imageJson, strlen(imageJson));
    } else if (strncmp(name, "/ok.json", sizeof("/ok.json")) == 0) {
        auto okMessage = "{\"status\": \"ok\"}";
        return get_file_contents(file, okMessage, strlen(okMessage));
    } else if (strncmp(name, "/wait.json", sizeof("/wait.json")) == 0) {
        auto waitMessage = "{\"status\": \"wait\"}";
        return get_file_contents(file, waitMessage, strlen(waitMessage));
    } else if (strncmp(name, "/overflow.json", sizeof("/overflow.json")) == 0) {
        auto overflowMessage = "{\"status\": \"overflow\"}";
        return get_file_contents(file, overflowMessage, strlen(overflowMessage));
    } else if (strncmp(name, "/done.json", sizeof("/done.json")) == 0) {
        auto doneMessage = "{\"status\": \"done\"}";
        return get_file_contents(file, doneMessage, strlen(doneMessage));
    } else if (strncmp(name, "/error.json", sizeof("/error.json")) == 0) {
        auto errorMessage = "{\"status\": \"error\"}";
        return get_file_contents(file, errorMessage, strlen(errorMessage));
    } else if (strncmp(name, "/index.html", sizeof("/index.html")) == 0) {
        return get_file_contents(file, index_html, strlen(index_html));
    } else if (strncmp(name, "/fw_upgrade.html", sizeof("/fw_upgrade.html")) == 0) {
        return get_file_contents(file, fw_upgrade_html, strlen(fw_upgrade_html));
    } else if (strncmp(name, "/slimselect.css", sizeof("/slimselect.css")) == 0) {
        return get_file_contents(file, slimselect_css, strlen(slimselect_css));
    } else if (strncmp(name, "/slimselect.js", sizeof("/slimselect.js")) == 0) {
        return get_file_contents(file, slimselect_js, strlen(slimselect_js));
    } else if (strncmp(name, "/control.js", sizeof("/control.js")) == 0) {
        return get_file_contents(file, control_js, strlen(control_js));
    } else if (strncmp(name, "/style.css", sizeof("/style.css")) == 0) {
        return get_file_contents(file, style_css, strlen(style_css));
    } else if (strncmp(name, "/filenames.json", sizeof("/filenames.json")) == 0) {
        if (g_filenames_serving_id >= MAX_SCSI_IDS || g_filenames_scsi_id[g_filenames_serving_id] == nullptr)
            return 0;
        const char *json = g_filenames_scsi_id[g_filenames_serving_id];
        return get_file_contents(file, json, strlen(json));
    } else if (strncmp(name, "/nextImage.json", sizeof("/nextImage.json")) == 0) {
        char *image;
        if (queue_try_remove(&imageQueue, &image)) {
            int retVal = get_file_contents(file, image, strlen(image));
            delete[] image;
            return retVal;
        }
        return 0;
    } else if (strncmp(name, "/version.js", sizeof("/version.js")) == 0) {
        return get_file_contents(file, version_js, strlen(version_js));
    } else if (strncmp(name, "/version.json", sizeof("/version.json")) == 0) {
        return get_file_contents(file, versionJson, strlen(versionJson));
    } else if (strncmp(name, "/devicelist.json", sizeof("/devicelist.json")) == 0) {
        return get_file_contents(file, deviceListJson, strlen(deviceListJson));
    } else {
        printf("Unable to find %s\n", name);
        return 0;
    }
}

void fs_close_custom(struct fs_file *file) {
    printf("close custom closing file\n");
}

int fs_read_custom(struct fs_file *file, char *buffer, int count)
{
    if (file->index >= file->len) return FS_READ_EOF;
    int read = (file->len - file->index < count) ? file->len - file->index : count;
    memcpy(buffer, (char*)file->pextension + file->index, read);
    file->index += read;
    return read;
}

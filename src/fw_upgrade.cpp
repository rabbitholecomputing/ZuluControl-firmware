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

#include "fw_upgrade.h"
#include "ZuluControlI2CClient.h"
#include "display/display_task.h"  // PumpFirmwareUpgradeDisplay()
#include <hardware/sync.h>
#include <hardware/flash.h>
#include <hardware/gpio.h>
#include <hardware/structs/scb.h>
#include <pico/multicore.h>
#include <pico/time.h>
#include "lwip/apps/fs.h"
#include "lwip/apps/httpd.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/opt.h"
#include "boot/uf2.h"
#include <string.h>

// User LED GPIO, defined in main.cpp -- gpio_put() on a pin whose function/
// direction hasn't been configured (non-board-type-B boards) is a harmless
// no-op, same assumption the rest of the LED call sites in main.cpp rely on.
extern const uint8_t GPIO_MCU_LED;

#if PICO_RP2350
#define UF2_FAMILY_ID RP2350_ARM_S_FAMILY_ID
#else
#define UF2_FAMILY_ID RP2040_FAMILY_ID
#endif

// This is constant for RP2xxx
#define UF2_PAYLOAD_SIZE 256

// Uploaded data is temporarily stored at 1 MB offset from flash start.
// It is only flashed to main location after the whole image is successfully received.
#define FW_UPGRADE_TEMP_OFFSET (1024 * 1024)
#define FW_UPGRADE_TARGET_ADDR 0x10000000

// 4kB erase blocks are large enough for all flash chips
#define FLASH_SECTOR_ERASE_SIZE 4096u

static struct {
    size_t block_size;
    uf2_block block;
    uint32_t blocks_received;
    uint32_t num_blocks;
    uint8_t staged[MAX_MSG_SIZE];
    size_t staged_len;
    // Progress mirror for the control-panel display (see fwupgrade_get_status).
    bool active;
    bool via_http;
    uint32_t retries;
    uint32_t errors;
    // Full-file (byte-based) progress, independent of the family-matching
    // blocks_received/num_blocks used for flashing -- see FwUpgradeStatus.
    uint64_t bytes_received;
    uint64_t total_bytes;
} g_fwup_state;

void fwupgrade_get_status(FwUpgradeStatus *out)
{
    if (!out) return;
    out->active = g_fwup_state.active;
    out->via_http = g_fwup_state.via_http;
    out->bytes_received = g_fwup_state.bytes_received;
    out->total_bytes = g_fwup_state.total_bytes;
    out->retries = g_fwup_state.retries;
    out->errors = g_fwup_state.errors;
}

err_t fwupgrade_post_begin(void *connection, const char *uri, const char *http_request,
                       u16_t http_request_len, int content_len, char *response_uri,
                       u16_t response_uri_len, u8_t *post_auto_wnd)
{
    printf("fwupgrade_post_begin %s\n", uri);
    g_fwup_state.block_size = 0;
    g_fwup_state.blocks_received = 0;
    g_fwup_state.num_blocks = 0;
    g_fwup_state.active = true;
    g_fwup_state.via_http = true;
    g_fwup_state.retries = 0;
    g_fwup_state.errors = 0;
    g_fwup_state.bytes_received = 0;
    // The web upload POSTs the raw .uf2 as the body, so Content-Length is the
    // exact full-file size -- the whole universal image, both families. (<= 0
    // means the length wasn't supplied; fall back to deriving it per-block.)
    g_fwup_state.total_bytes = (content_len > 0) ? (uint64_t)content_len : 0;
    gpio_put(GPIO_MCU_LED, false);
    return ERR_OK;
}

void fwupgrade_i2c_begin()
{
    printf("I2C firmware upgrade begining\n");
    g_fwup_state.block_size = 0;
    g_fwup_state.blocks_received = 0;
    g_fwup_state.num_blocks = 0;
    g_fwup_state.staged_len = 0;
    g_fwup_state.active = true;
    g_fwup_state.via_http = false;
    g_fwup_state.retries = 0;
    g_fwup_state.errors = 0;
    g_fwup_state.bytes_received = 0;
    // No content length over I2C -- derive the total from the first UF2 block's
    // num_blocks the first time we parse one (see handle_uf2_block).
    g_fwup_state.total_bytes = 0;
    gpio_put(GPIO_MCU_LED, false);
}

void fwupgrade_i2c_abort()
{
    g_fwup_state.block_size = 0;
    g_fwup_state.blocks_received = 0;
    g_fwup_state.num_blocks = 0;
    g_fwup_state.staged_len = 0;
    g_fwup_state.active = false;
    g_fwup_state.bytes_received = 0;
    g_fwup_state.total_bytes = 0;
    gpio_put(GPIO_MCU_LED, false);
}


// Erase whole flash area before programming it
__attribute__((section(".time_critical.erase_flash_area")))
static void erase_flash_area(uint32_t offset, uint32_t len)
{
    if (len % FLASH_SECTOR_ERASE_SIZE != 0)
    {
        len += FLASH_SECTOR_ERASE_SIZE - (len % FLASH_SECTOR_ERASE_SIZE);
    }

    uint32_t saved_irq = save_and_disable_interrupts();
    flash_range_erase(offset, len);
    restore_interrupts(saved_irq);
}

// Program one flash block
__attribute__((section(".time_critical.program_flash_block")))
static bool program_flash_block(uint32_t offset, uint8_t *data)
{
    uint32_t saved_irq = save_and_disable_interrupts();
    flash_range_program(offset, data, UF2_PAYLOAD_SIZE);
    restore_interrupts(saved_irq);
    bool success = (memcmp(data, (void*)(XIP_NOCACHE_NOALLOC_BASE + offset), UF2_PAYLOAD_SIZE) == 0);
    return success;
}

// Copy firmware from temporary area to final flash location.
// Note: this must be fully in RAM and not call any flash functions,
// because the flash is being overwritten.
__attribute__((section(".time_critical.finish_fw_upgrade")))
int64_t finish_fw_upgrade(alarm_id_t id, void *user_data)
{
    uint32_t total_fw_size = g_fwup_state.num_blocks * UF2_PAYLOAD_SIZE;
    if (total_fw_size < 16 * 1024 ||
        total_fw_size > FW_UPGRADE_TEMP_OFFSET)
    {
        return 0; // Just a sanity check
    }

    save_and_disable_interrupts();

    // Copy firmware from temp area to the final offset
    erase_flash_area(0, total_fw_size);
    for (uint32_t block = 0; block < g_fwup_state.num_blocks; block++)
    {
        // We need to copy each block to RAM and from there back to flash
        // at the final location. But memcpy might not be in RAM, so do it manually.
        uint8_t buf[UF2_PAYLOAD_SIZE];
        uint32_t offset = block * UF2_PAYLOAD_SIZE;
        uint8_t *src = (uint8_t*)(FW_UPGRADE_TARGET_ADDR + FW_UPGRADE_TEMP_OFFSET + offset);
        for (size_t i = 0; i < UF2_PAYLOAD_SIZE; i++)
        {
            buf[i] = src[i];
        }

        flash_range_program(offset, buf, UF2_PAYLOAD_SIZE);
    }

    // Reboot
    scb_hw->aircr = 0x05FA0004;
    while(1);
}

static bool handle_uf2_block(uf2_block *block, bool stop_second_core = true)
{
    static uint32_t last_progress_log_ms = 0;
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());

    if (block->magic_start0 != UF2_MAGIC_START0 ||
        block->magic_start1 != UF2_MAGIC_START1 ||
        block->magic_end != UF2_MAGIC_END)
    {
        printf("UF2 magics do not match (0x%08lx 0x%08lx 0x%08lx)\n",
            block->magic_start0, block->magic_start1, block->magic_end);
        g_fwup_state.errors++;
        return false;
    }

    // The I2C path has no Content-Length, so the total file size is discovered
    // from the UF2 stream itself. A universal image is several independently
    // numbered family segments concatenated; each segment starts at block_no 0
    // and declares its own num_blocks. Summing num_blocks at every block_no==0
    // totals the WHOLE image, not just the first family -- otherwise the bar
    // hits 100% at the first seam (~half the upload) and sticks there. Each UF2
    // block is sizeof(uf2_block) bytes on the wire. (Retried chunks are
    // discarded before commit, so a segment's block_no==0 is parsed only once.)
    // The web path already has the exact size from Content-Length -- leave it be.
    if (!g_fwup_state.via_http && block->block_no == 0 && block->num_blocks > 0)
        g_fwup_state.total_bytes += (uint64_t)block->num_blocks * sizeof(uf2_block);

    static uint32_t s_last_family_block = UF2_FAMILY_ID;
    static uint8_t s_dot_count = 0;

    if (block->file_size != UF2_FAMILY_ID || !(block->flags & UF2_FLAG_FAMILY_ID_PRESENT))
    {
        if (s_last_family_block != block->file_size)
        {
            printf("Ignoring block for different family id (expected 0x%08x, got 0x%08lx)\n",
                UF2_FAMILY_ID, block->file_size);
            s_last_family_block =  block->file_size;
        }
        else if ((uint32_t)(now_ms - last_progress_log_ms) >= 2000)
        {
            last_progress_log_ms = now_ms;
            printf(".");
            if (s_dot_count > 20)
            {
                printf("\n");
                s_dot_count = 0;
            }
            else
            {
                s_dot_count++;
            }
        }

        // Continue upload until we get a block for us in a universal binary
        return true;
    }
    else
    {
        s_dot_count = 0;
    }

    if (block->flags & UF2_FLAG_NOT_MAIN_FLASH)
    {
        printf("Ignoring not-for-flash UF2 block\n");
        return true;
    }

    if (block->payload_size != UF2_PAYLOAD_SIZE)
    {
        printf("Unexpected payload size %lu\n", block->payload_size);
        g_fwup_state.errors++;
        return false;
    }

    if (block->target_addr < FW_UPGRADE_TARGET_ADDR ||
        block->target_addr >= FW_UPGRADE_TARGET_ADDR + FW_UPGRADE_TEMP_OFFSET)
    {
        printf("UF2 block offset out of range: 0x%08lx\n", block->target_addr);
        g_fwup_state.errors++;
        return false;
    }

    if (block->block_no == 0)
    {
        printf("Got first UF2 block, total %lu blocks\n", block->num_blocks);
        g_fwup_state.blocks_received = 0;
        g_fwup_state.num_blocks = block->num_blocks;

        if (stop_second_core)
        {
            printf("Stopping second core\n");
            multicore_reset_core1();
        }
    }
    else if (block->block_no != g_fwup_state.blocks_received)
    {
        printf("UF2 block out of order, got %lu expected %lu\n",
            block->block_no, g_fwup_state.blocks_received);
        g_fwup_state.errors++;
        return false;
    }

    uint32_t block_offset = block->target_addr - FW_UPGRADE_TARGET_ADDR;
    uint32_t tmp_addr = FW_UPGRADE_TEMP_OFFSET + block_offset;

    // Core1 lockout (when needed) is acquired once by the caller around the
    // whole run of blocks in a commit, not per block -- see fwupgrade_i2c_commit_staged.

    // Erase one sector at a time just before it is first needed.
    // This keeps each interrupt-disabled period under ~50ms instead of
    // erasing the entire temp area (5+ seconds) upfront on block 0.
    if (block_offset % FLASH_SECTOR_ERASE_SIZE == 0)
    {
        erase_flash_area(tmp_addr, FLASH_SECTOR_ERASE_SIZE);
    }
    bool program_ok = program_flash_block(tmp_addr, block->data);
    if (!program_ok)
    {
        printf("Programming UF2 block to temporary flash failed at addr %lu\n", tmp_addr);
        g_fwup_state.errors++;
        return false;
    }
    g_fwup_state.blocks_received++;

    // Fast LED blink, one toggle per flash block written.
    gpio_put(GPIO_MCU_LED, (g_fwup_state.blocks_received & 1) != 0);

    // Log progress at most every 2 seconds rather than every block
    bool last_block = (g_fwup_state.blocks_received == block->num_blocks);
    if ((uint32_t)(now_ms - last_progress_log_ms) >= 2000 || last_block)
    {
        last_progress_log_ms = now_ms;
        uint32_t percent = block->num_blocks ? (g_fwup_state.blocks_received * 100 / block->num_blocks) : 0;
        printf("Programming UF2 block %lu/%lu (%lu%%)\n",
               g_fwup_state.blocks_received, block->num_blocks, percent);
    }

    return true;
}

err_t fwupgrade_post_receive_data(void *connection, struct pbuf *p)
{
    uint8_t *first_byte = (uint8_t*)p->payload;
    printf("fwupgrade_post_receive_data %d 0x%02x 0x%02x\n", (int)p->tot_len, first_byte[0], first_byte[1]);

    // Count the whole segment toward full-file upload progress (all families).
    // The POST body is the raw .uf2, so this sums to Content-Length at the end.
    g_fwup_state.bytes_received += p->tot_len;

    // Walk every segment in the pbuf chain — lwIP may deliver chained pbufs
    // after TCP retransmissions, and p->payload/p->len only covers the first segment.
    for (struct pbuf *q = p; q != NULL; q = q->next)
    {
        uint8_t *data = (uint8_t*)q->payload;
        size_t remain = q->len;

        while (remain > 0)
        {
            size_t block_remain = sizeof(uf2_block) - g_fwup_state.block_size;
            size_t to_cpy = (remain > block_remain) ? block_remain : remain;
            memcpy((uint8_t*)&g_fwup_state.block + g_fwup_state.block_size, data, to_cpy);
            g_fwup_state.block_size += to_cpy;
            data += to_cpy;
            remain -= to_cpy;

            if (g_fwup_state.block_size == sizeof(uf2_block))
            {
                if (!handle_uf2_block(&g_fwup_state.block))
                {
                    printf("handle_uf2_block() failed\n");
                    pbuf_free(p);
                    return ERR_VAL;
                }
                g_fwup_state.block_size = 0;
            }
        }
    }

    pbuf_free(p);

    // A web upload runs entirely in the lwIP background context, which starves
    // core0's main loop -- so DisplayControlTask() can't advance the progress
    // bar on its own. Drive it from here, once per received segment (the pump
    // throttles itself), so the panel shows real progress instead of a frozen
    // first frame. Harmless no-op if there's no display or no active upgrade.
    zuluide::display::PumpFirmwareUpgradeDisplay();

    return ERR_OK;
}

bool fwupgrade_i2c_stage_chunk(const uint8_t *data, size_t length)
{
    if (length > sizeof(g_fwup_state.staged))
    {
        printf("fwupgrade_i2c_stage_chunk: length %zu exceeds staging buffer\n", length);
        return false;
    }

    memcpy(g_fwup_state.staged, data, length);
    g_fwup_state.staged_len = length;
    return true;
}

bool fwupgrade_i2c_commit_staged()
{
    if (g_fwup_state.staged_len == 0)
    {
        return true;
    }

    const uint8_t *data = g_fwup_state.staged;
    size_t remain = g_fwup_state.staged_len;
    g_fwup_state.staged_len = 0;

    // Committed (confirmed-good) chunk bytes count toward upload progress. Doing
    // it here rather than at stage time means a chunk the host RETRYs (discarded
    // before commit) isn't double-counted when it's resent.
    g_fwup_state.bytes_received += remain;

    // Core1 (running the I2C slave ISR) is left running during the I2C upgrade
    // path so it can keep servicing the bus while we flash. Core1 executes from
    // flash just like core0, so it must be safely parked in RAM (via the lockout
    // mechanism) for the duration of any erase/program call, or it can stall/hang
    // fetching an instruction from flash mid-erase. Acquired once here for the
    // whole staged chunk (all its uf2_blocks) instead of once per block, since
    // each lockout round trip costs a fixed signalling overhead independent of
    // how much flash work happens while it's held.
    bool locked = multicore_lockout_start_timeout_us(2000000);
    if (!locked) printf("Lockout start failed\n");

    bool ok = true;
    while (remain > 0)
    {
        size_t block_remain = sizeof(uf2_block) - g_fwup_state.block_size;
        size_t to_cpy = (remain > block_remain) ? block_remain : remain;
        memcpy((uint8_t*)&g_fwup_state.block + g_fwup_state.block_size, data, to_cpy);
        g_fwup_state.block_size += to_cpy;
        data += to_cpy;
        remain -= to_cpy;
        if (g_fwup_state.block_size == sizeof(uf2_block))
        {
            if (!handle_uf2_block(&g_fwup_state.block, false))  // Don't stop multicore for I2C
            {
                printf("handle_uf2_block() failed\n");
                ok = false;
                break;
            }
            g_fwup_state.block_size = 0;
        }
    }

    bool unlocked = multicore_lockout_end_timeout_us(2000000);
    if (!unlocked) printf("Lockout end failed\n");
    // multicore_lockout_end_* only confirms core0's own signal was sent, not that
    // core1 actually finished exiting its wait loop and resumed normal execution.
    // Give it a guaranteed window to do so before touching it again.
    sleep_us(1000);

    return ok;
}

void fwupgrade_i2c_discard_staged()
{
    g_fwup_state.staged_len = 0;
    g_fwup_state.retries++;
}
void start_multicore_i2c();

void fwupgrade_post_finished(void *connection, char *response_uri, u16_t response_uri_len)
{
    if (g_fwup_state.num_blocks == 0 ||
        g_fwup_state.blocks_received != g_fwup_state.num_blocks)
    {
        printf("fwupgrade interrupted, %lu/%lu blocks done\n",
            g_fwup_state.blocks_received, g_fwup_state.num_blocks);
        gpio_put(GPIO_MCU_LED, false);
        g_fwup_state.active = false;

        if (g_fwup_state.num_blocks > 0)
        {
            // We reset multicore so restore it back to operation
            start_multicore_i2c();
        }
    }
    else
    {
        printf("fwupgrade_post_finished\n");
        snprintf(response_uri, response_uri_len, "/fw_upgrade.html");

        // Let lwip post the result and then proceed to copy the firmware to the actual
        // location and reboot.
        add_alarm_in_ms(500, finish_fw_upgrade, NULL, false);
    }
}


void fwupgrade_i2c_finished()
{
    fwupgrade_i2c_commit_staged();

    if (g_fwup_state.num_blocks == 0 ||
        g_fwup_state.blocks_received != g_fwup_state.num_blocks)
    {
        printf("I2C firmware upgrade interrupted, %lu/%lu blocks done\n",
            g_fwup_state.blocks_received, g_fwup_state.num_blocks);
        gpio_put(GPIO_MCU_LED, false);
        g_fwup_state.active = false;

        if (g_fwup_state.num_blocks > 0)
        {
            // We reset multicore so restore it back to operation
            start_multicore_i2c();
        }
    }
    else
    {
        // Tell the host the update succeeded and this device is about to
        // reboot into it, via a sentinel ACK the host recognizes as
        // distinct from a normal chunk ack: length 0xFFFF, crc32
        // 0x00000000. No real chunk ack can ever produce that length --
        // chunks are capped at I2C_UPGRADE_MAX_CHUNK bytes -- so it can't
        // be confused with one. Sent through the same ACK command/queue as
        // a normal chunk ack, so the host's existing
        // UpgradeZuluControlFwReadAck() reads it with no changes needed.
        uint8_t sentinelAck[6] = { 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00 };
        zuluide::i2c::client::EnqueueRequestBinary(I2C_CLIENT_UPDATE_FW_ACK, sentinelAck, sizeof(sentinelAck));

        // Core1 drives the I2C slave ISR that actually puts bytes on the
        // wire -- give the ACK a chance to be fully sent before resetting
        // it below, or the host would never see it.
        zuluide::i2c::client::WaitForOutputQueueDrain(2000);

        printf("I2C firmware upgrade finished. Rebooting\n");

        // location and reboot.
        multicore_reset_core1();
        add_alarm_in_ms(500, finish_fw_upgrade, NULL, false);
    }
}
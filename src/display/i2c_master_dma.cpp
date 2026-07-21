/**
 * ZuluIDE™ - Copyright (c) 2026 Rabbit Hole Computing™
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

#include "i2c_master_dma.h"
#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/structs/i2c.h>
#include <hardware/regs/i2c.h>
#include <pico/time.h>

namespace zuluide::display {

namespace {

i2c_inst_t *g_i2c = nullptr;
int g_hdr_channel = -1;
int g_data_channel = -1;
bool g_initialized = false;
bool g_write_in_flight = false;
absolute_time_t g_write_until;

// Referenced asynchronously by DMA hardware -- must have a stable address
// for the duration of a transfer, so file-scope statics rather than
// stack-local. Widened to 16 bits so the STOP control bit (bit 9 of
// IC_DATA_CMD) can be embedded in the last payload word, same trick as
// related/ZuluIDE-firmware's i2c_master_dma.cpp.
uint16_t g_header_word[1];
uint16_t g_data_words[I2C_DISPLAY_MAX_PAYLOAD];

}  // namespace

bool I2CMasterDmaInit(i2c_inst_t *i2c, unsigned int sdaPin, unsigned int sclPin, unsigned int baudrate)
{
    g_i2c = i2c;
    i2c_init(i2c, baudrate);
    gpio_set_function(sdaPin, GPIO_FUNC_I2C);
    gpio_set_function(sclPin, GPIO_FUNC_I2C);
    gpio_pull_up(sdaPin);
    gpio_pull_up(sclPin);

    g_hdr_channel = dma_claim_unused_channel(true);
    g_data_channel = dma_claim_unused_channel(true);
    g_initialized = (g_hdr_channel >= 0 && g_data_channel >= 0);
    return g_initialized;
}

bool I2CMasterDmaIsBusy()
{
    return g_write_in_flight;
}

bool I2CMasterDmaStartWrite(uint8_t addr, uint8_t controlByte, const uint8_t *payload, size_t length)
{
    if (!g_initialized || g_write_in_flight || length == 0 || length > I2C_DISPLAY_MAX_PAYLOAD)
        return false;

    i2c_inst_t *i2c = g_i2c;

    g_header_word[0] = controlByte;
    for (size_t i = 0; i < length; i++)
        g_data_words[i] = payload[i];
    // STOP goes on the last word of the transfer (see WaitForChannelOrAbort's
    // comment in Poll() below for why dma_channel_is_busy() alone isn't enough).
    g_data_words[length - 1] |= I2C_IC_DATA_CMD_STOP_BITS;

    dma_channel_config hdr_cfg = dma_channel_get_default_config(g_hdr_channel);
    channel_config_set_transfer_data_size(&hdr_cfg, DMA_SIZE_16);
    channel_config_set_read_increment(&hdr_cfg, true);
    channel_config_set_write_increment(&hdr_cfg, false);
    channel_config_set_dreq(&hdr_cfg, i2c_get_dreq(i2c, true));
    channel_config_set_chain_to(&hdr_cfg, g_data_channel);
    dma_channel_configure(g_hdr_channel, &hdr_cfg, &i2c_get_hw(i2c)->data_cmd, g_header_word, 1, false);

    dma_channel_config data_cfg = dma_channel_get_default_config(g_data_channel);
    channel_config_set_transfer_data_size(&data_cfg, DMA_SIZE_16);
    channel_config_set_read_increment(&data_cfg, true);
    channel_config_set_write_increment(&data_cfg, false);
    channel_config_set_dreq(&data_cfg, i2c_get_dreq(i2c, true));
    channel_config_set_chain_to(&data_cfg, g_data_channel);  // no further chaining
    dma_channel_configure(g_data_channel, &data_cfg, &i2c_get_hw(i2c)->data_cmd, g_data_words, length, false);

    i2c_get_hw(i2c)->enable = 0;
    i2c_get_hw(i2c)->tar = addr;
    i2c_get_hw(i2c)->dma_cr = I2C_IC_DMA_CR_TDMAE_BITS;
    i2c_get_hw(i2c)->enable = 1;

    // Clear any stale STOP_DET/TX_ABRT left set by a prior transaction --
    // Poll() below treats these as proof *this* transaction completed, so a
    // leftover bit would report success instantly, before any real bytes go out.
    i2c_get_hw(i2c)->clr_stop_det;
    i2c_get_hw(i2c)->clr_tx_abrt;

    g_write_until = make_timeout_time_ms(100);
    g_write_in_flight = true;

    dma_channel_start(g_hdr_channel);  // chains automatically into g_data_channel
    return true;
}

bool I2CMasterDmaPoll(bool *out_ok)
{
    if (!g_write_in_flight)
        return false;

    i2c_inst_t *i2c = g_i2c;

    if (dma_channel_is_busy(g_data_channel))
    {
        if (i2c_get_hw(i2c)->raw_intr_stat & I2C_IC_RAW_INTR_STAT_TX_ABRT_BITS)
        {
            i2c_get_hw(i2c)->clr_tx_abrt;
            dma_channel_abort(g_data_channel);
            dma_channel_abort(g_hdr_channel);
        }
        else if (absolute_time_diff_us(get_absolute_time(), g_write_until) >= 0)
        {
            return false;  // still legitimately in flight
        }
        else
        {
            dma_channel_abort(g_data_channel);
            dma_channel_abort(g_hdr_channel);
        }
    }

    // dma_channel_is_busy() only reflects the DMA engine finishing *writing*
    // into the TX FIFO -- not the I2C peripheral finishing *shifting those
    // bytes out over the wire*. Wait for the actual STOP condition before
    // declaring success (mirrors i2c_master_dma.cpp's WaitForChannelOrAbort,
    // just spread across Poll() calls instead of busy-waiting inline).
    bool stopped = i2c_get_hw(i2c)->raw_intr_stat & I2C_IC_RAW_INTR_STAT_STOP_DET_BITS;
    bool aborted = i2c_get_hw(i2c)->raw_intr_stat & I2C_IC_RAW_INTR_STAT_TX_ABRT_BITS;
    bool timedOut = absolute_time_diff_us(get_absolute_time(), g_write_until) < 0;

    if (!stopped && !aborted && !timedOut)
        return false;

    i2c_get_hw(i2c)->dma_cr = 0;
    if (aborted)
        i2c_get_hw(i2c)->clr_tx_abrt;
    if (stopped)
        i2c_get_hw(i2c)->clr_stop_det;

    g_write_in_flight = false;
    if (out_ok)
        *out_ok = stopped && !aborted;
    return true;
}

bool I2CMasterWriteThenRead(uint8_t addr, const uint8_t *writeData, size_t writeLen,
                            uint8_t *readData, size_t readLen, absolute_time_t until)
{
    if (!g_initialized || g_write_in_flight)
        return false;

    // `true` (nostop) keeps the bus held with a repeated START into the
    // read that follows, instead of releasing it with a STOP -- faster and
    // avoids a gap where the expander's latched input could change between
    // the two phases of what is logically one register-read transaction.
    int wr = i2c_write_blocking_until(g_i2c, addr, writeData, writeLen, true, until);
    if (wr != (int)writeLen)
        return false;

    int rd = i2c_read_blocking_until(g_i2c, addr, readData, readLen, false, until);
    return rd == (int)readLen;
}

}  // namespace zuluide::display

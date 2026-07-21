#include <string.h>
#include "ad7124.h"


// -------------------------------
// Internal helpers
// -------------------------------

// Build communications register byte:
// Bit7 WEN must be 0, Bit6 R/W, Bits5:0 address.
static inline uint8_t ad7124_build_comm(ad7124_reg_t reg, bool read)
{
    return (uint8_t)(((read ? 1 : 0) << 6) | ((uint8_t)reg & 0x3F));
}

static bool ad7124_write_bytes(ad7124_dev_t *dev, ad7124_reg_t reg, const uint8_t *data, size_t n)
{
    if (!dev || (!data && n > 0) || n > 4) {
        return false;
    }

    uint8_t frame[1 + 4] = {0};
    frame[0] = ad7124_build_comm(reg, false);
    if (n > 0) {
        memcpy(&frame[1], data, n);
    }
    return bus_spi_write(&dev->spi_bus, frame, 1 + n);
}

static bool ad7124_read_bytes(ad7124_dev_t *dev, ad7124_reg_t reg, uint8_t *data, size_t n)
{
    if (!dev || !data || n == 0 || n > 4) {
        return false;
    }

    uint8_t cmd = ad7124_build_comm(reg, true);
    if (!bus_spi_begin(&dev->spi_bus)) {
        return false;
    }

    bool ok = bus_spi_transfer(&dev->spi_bus, &cmd, NULL, 1);
    if (ok) {
        ok = bus_spi_transfer(&dev->spi_bus, NULL, data, n);
    }
    bool ended = bus_spi_end(&dev->spi_bus);
    return ok && ended;
}

// -------------------------------
// Public API
// -------------------------------

// Initialize sriver defaults.
void ad7124_init(ad7124_dev_t *ctx)
{
    ctx->data_status_enabled = false;
}


bool ad7124_reset(ad7124_dev_t *dev)
{
    if (!dev) return false;

    // Interface reset by writing at least 64 SCLKs with DIN high.
    uint8_t ones[8];
    memset(ones, 0xFF, sizeof(ones));
    return bus_spi_write(&dev->spi_bus, ones, sizeof(ones));
}

bool ad7124_read_id(ad7124_dev_t *dev, uint8_t *id_out)
{
    if (!id_out) return false;
    // ID register expected 0x17. :contentReference[oaicite:7]{index=7}
    return ad7124_read_reg8(dev, AD7124_REG_ID, id_out);
}

// ---- 8/16/24-bit accessors ----

bool ad7124_read_reg8(ad7124_dev_t *dev, ad7124_reg_t reg, uint8_t *val)
{
    if (!val) return false;
    return ad7124_read_bytes(dev, reg, val, 1);
}

bool ad7124_read_reg16(ad7124_dev_t *dev, ad7124_reg_t reg, uint16_t *val)
{
    if (!val) return false;
    uint8_t b[2];
    if (!ad7124_read_bytes(dev, reg, b, 2)) return false;
    *val = (uint16_t)((b[0] << 8) | b[1]);
    return true;
}

bool ad7124_read_reg24(ad7124_dev_t *dev, ad7124_reg_t reg, uint32_t *val)
{
    if (!val) return false;
    uint8_t b[3];
    if (!ad7124_read_bytes(dev, reg, b, 3)) return false;
    *val = ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | b[2];
    return true;
}

bool ad7124_write_reg8(ad7124_dev_t *dev, ad7124_reg_t reg, uint8_t val)
{
    return ad7124_write_bytes(dev, reg, &val, 1);
}

bool ad7124_write_reg16(ad7124_dev_t *dev, ad7124_reg_t reg, uint16_t val)
{
    uint8_t b[2] = {(uint8_t)(val >> 8), (uint8_t)(val & 0xFF)};
    return ad7124_write_bytes(dev, reg, b, 2);
}

bool ad7124_write_reg24(ad7124_dev_t *dev, ad7124_reg_t reg, uint32_t val)
{
    uint8_t b[3] = {(uint8_t)(val >> 16), (uint8_t)(val >> 8), (uint8_t)(val & 0xFF)};
    return ad7124_write_bytes(dev, reg, b, 3);
}

// -------------------------------
// Configuration
// -------------------------------

bool ad7124_config_adc_control(ad7124_dev_t *dev,
                               ad7124_power_mode_t pwr,
                               ad7124_operating_mode_t mode,
                               bool data_status,
                               bool ref_en)
{
    // ADC_CONTROL is 16-bit. Fields per Table 67. :contentReference[oaicite:8]{index=8}
    //
    // Bits:
    // 12 DOUT_RDY_DEL (leave 0)
    // 11 CONT_READ (leave 0)
    // 10 DATA_STATUS
    // 9  CS_EN (leave 0)
    // 8  REF_EN
    // 7:6 POWER_MODE
    // 5:2 Mode
    // 1:0 CLK_SEL (00 internal)
    //
    uint16_t v = 0;
    v |= (uint16_t)(data_status ? (1u << 10) : 0);
    v |= (uint16_t)(ref_en ? (1u << 8) : 0);

    v |= (uint16_t)((pwr & 0x3) << 6);
    v |= (uint16_t)((mode & 0xF) << 2);

    // CLK_SEL = 00 internal 614.4 kHz
    dev->data_status_enabled = data_status;

    return ad7124_write_reg16(dev, AD7124_REG_ADC_CONTROL, v);
}

bool ad7124_config_setup(ad7124_dev_t *dev, uint8_t setup,
                         ad7124_ref_sel_t ref_sel,
                         bool bipolar,
                         ad7124_pga_t pga,
                         bool ain_bufp, bool ain_bufm,
                         bool ref_bufp, bool ref_bufm,
                         uint8_t burnout)
{
    if (setup > 7) return false;
    if (burnout > 3) burnout = 0;

    // CONFIG_x is 16-bit.
    // Fields per Table 75. :contentReference[oaicite:9]{index=9}
    //
    // 11 Bipolar
    // 10:9 Burnout
    // 8 REF_BUFP
    // 7 REF_BUFM
    // 6 AIN_BUFP
    // 5 AIN_BUFM
    // 4:3 REF_SEL
    // 2:0 PGA
    //
    uint16_t v = 0;
    v |= (uint16_t)(bipolar ? (1u << 11) : 0);
    v |= (uint16_t)((burnout & 0x3) << 9);

    v |= (uint16_t)(ref_bufp ? (1u << 8) : 0);
    v |= (uint16_t)(ref_bufm ? (1u << 7) : 0);
    v |= (uint16_t)(ain_bufp ? (1u << 6) : 0);
    v |= (uint16_t)(ain_bufm ? (1u << 5) : 0);

    v |= (uint16_t)((ref_sel & 0x3) << 3);
    v |= (uint16_t)(pga & 0x7);

    return ad7124_write_reg16(dev, AD7124_REG_CONFIG(setup), v);
}

bool ad7124_config_filter(ad7124_dev_t *dev, uint8_t setup,
                          ad7124_filter_type_t filter,
                          uint8_t post_filter,
                          bool rej60,
                          bool single_cycle,
                          uint16_t fs)
{
    if (setup > 7) return false;
    if (fs < 1) fs = 1;
    if (fs > 2047) fs = 2047;
    post_filter &= 0x7;

    // FILTER_x is 24-bit.
    // Layout per Table 76. :contentReference[oaicite:10]{index=10}
    //
    // 23:21 Filter type
    // 20 REJ60
    // 19:17 POST_FILTER
    // 16 SINGLE_CYCLE
    // 10:0 FS
    //
    uint32_t v = 0;
    v |= ((uint32_t)(filter & 0x7) << 21);
    v |= (rej60 ? (1u << 20) : 0);
    v |= ((uint32_t)post_filter << 17);
    v |= (single_cycle ? (1u << 16) : 0);
    v |= (uint32_t)(fs & 0x7FF);

    return ad7124_write_reg24(dev, AD7124_REG_FILTER(setup), v);
}

bool ad7124_config_channel(ad7124_dev_t *dev, uint8_t ch,
                           bool enable,
                           uint8_t setup,
                           uint8_t ainp,
                           uint8_t ainm)
{
    if (ch > 15 || setup > 7 || ainp > 31 || ainm > 31) return false;

    // CHANNEL_x is 16-bit.
    // Fields per Table 74. :contentReference[oaicite:11]{index=11}
    //
    // 15 Enable
    // 14:12 Setup
    // 9:5 AINP
    // 4:0 AINM
    //
    uint16_t v = 0;
    v |= (uint16_t)(enable ? (1u << 15) : 0);
    v |= (uint16_t)((setup & 0x7) << 12);
    v |= (uint16_t)((ainp & 0x1F) << 5);
    v |= (uint16_t)(ainm & 0x1F);

    return ad7124_write_reg16(dev, AD7124_REG_CHANNEL(ch), v);
}

bool ad7124_set_iout(ad7124_dev_t *dev,
                     ad7124_iout_val_t iout0, uint8_t iout0_ain,
                     ad7124_iout_val_t iout1, uint8_t iout1_ain,
                     bool close_pdsw)
{
    // IO_CONTROL_1 is 24-bit.
    // IOUT1[13:11], IOUT0[10:8], IOUT1_CH[7:4], IOUT0_CH[3:0], PDSW bit 15. :contentReference[oaicite:12]{index=12}
    //
    // Channel select encoding for IOUTx is basically "which AIN pin". We'll assume 0..15 valid.
    if (iout0_ain > 15) iout0_ain = 0;
    if (iout1_ain > 15) iout1_ain = 0;

    uint32_t v = 0;
    v |= (close_pdsw ? (1u << 15) : 0);
    v |= ((uint32_t)(iout1 & 0x7) << 11);
    v |= ((uint32_t)(iout0 & 0x7) << 8);
    v |= ((uint32_t)(iout1_ain & 0xF) << 4);
    v |= ((uint32_t)(iout0_ain & 0xF) << 0);

    return ad7124_write_reg24(dev, AD7124_REG_IO_CONTROL_1, v);
}

// -------------------------------
// Sampling
// -------------------------------

bool ad7124_wait_ready(ad7124_dev_t *dev, uint32_t timeout_us)
{
    absolute_time_t deadline = make_timeout_time_us(timeout_us);

    while (!time_reached(deadline)) {
        uint8_t status = 0;
        if (!ad7124_read_reg8(dev, AD7124_REG_STATUS, &status)) {
            return false;
        }
        // RDY bit is bit7; low indicates a conversion complete. :contentReference[oaicite:13]{index=13}
        if ((status & 0x80) == 0) {
            return true;
        }
        tight_loop_contents();
    }
    return false;
}

bool ad7124_read_sample(ad7124_dev_t *dev, uint32_t *code, uint8_t *ch_active)
{
    if (!code) return false;
    
    
    
    if (dev->data_status_enabled) {
        // Read 24-bit data + 8-bit status while keeping CS asserted for the frame.
        uint8_t cmd = ad7124_build_comm(AD7124_REG_DATA, true);
        uint8_t rx[4] = {0};

        if (!bus_spi_begin(&dev->spi_bus)) return false;
        bool ok = bus_spi_transfer(&dev->spi_bus, &cmd, NULL, 1);
        if (ok) {
            ok = bus_spi_transfer(&dev->spi_bus, NULL, rx, sizeof(rx));
        }
        bool ended = bus_spi_end(&dev->spi_bus);
        if (!ok || !ended) return false;

        *code = ((uint32_t)rx[0] << 16) | ((uint32_t)rx[1] << 8) | rx[2];

        if (ch_active) {
            *ch_active = (uint8_t)(rx[3] & 0x0F); // CH_ACTIVE[3:0]
        }
        return true;
    } else {
        // 24-bit data only.
        uint32_t v = 0;
        if (!ad7124_read_reg24(dev, AD7124_REG_DATA, &v)) return false;
        *code = v;

        if (ch_active) {
            uint8_t status = 0;
            if (!ad7124_read_reg8(dev, AD7124_REG_STATUS, &status)) return false;
            *ch_active = (uint8_t)(status & 0x0F);
        }
        return true;
    }
}

// -------------------------------
// Convenience init for your temperature front end
// -------------------------------


bool ad7124_init_temp_frontend(ad7124_dev_t *dev,
                               bool use_internal_ref_for_tc,
                               uint16_t fs_tc,
                               uint16_t fs_rtd,
                               uint8_t rtd_count)
{

    if (!dev) return false;
    if (rtd_count > 8) rtd_count = 8;
    //cs_select_line(dev->spi_bus.cs_mux_id);
    if (!ad7124_reset(dev)) return false;
    //cs_deselect();
    //cs_select_line(dev->spi_bus.cs_mux_id);
    uint8_t id = 0;
    if (!ad7124_read_id(dev, &id)) return false;
    //cs_deselect();
    if (id != 0x17) return false;

    if (!ad7124_config_adc_control(dev,
                                   AD7124_POWER_MID,
                                   AD7124_MODE_CONTINUOUS,
                                   true, // DATA_STATUS
                                   use_internal_ref_for_tc)) {
        return false;
    }

    // ----------------
    // Setup 0: Thermocouples
    // ----------------
    ad7124_ref_sel_t tc_ref = use_internal_ref_for_tc ?
                              AD7124_REF_INTERNAL :
                              AD7124_REF_REFIN1;

    if (!ad7124_config_setup(dev, 0,
                             tc_ref,
                             true,            // bipolar
                             AD7124_PGA_32,
                             true, true,      // AIN buffers
                             false, false,    // REF buffers
                             0)) {
        return false;
    }

    if (!ad7124_config_filter(dev, 0,
                              AD7124_FILTER_SINC4,
                              0, false, false,
                              fs_tc)) {
        return false;
    }

    // ----------------
    // Setup 1: RTDs (ratiometric vs 2k)
    // Recommend enabling AIN buffers because AIN_R sees RTD_R via 100R + RC.
    // ----------------
    if (!ad7124_config_setup(dev, 1,
                             AD7124_REF_REFIN1,
                             false,           // unipolar
                             AD7124_PGA_1,
                             true, true,      // AIN buffers ON for RTD
                             false, false,
                             0)) {
        return false;
    }

    if (!ad7124_config_filter(dev, 1,
                              AD7124_FILTER_SINC4,
                              0, false, false,
                              fs_rtd)) {
        return false;
    }

    // Disable all channels first
    for (uint8_t ch = 0; ch < 16; ch++) {
        (void)ad7124_config_channel(dev, ch, false, 0, 0, 1);
    }

    // ----------------
    // Thermocouples
    // ----------------
    if (!ad7124_config_channel(dev, 0, true, 0, 0, 1)) return false; // TC0 AIN0/AIN1
    if (!ad7124_config_channel(dev, 1, true, 0, 2, 3)) return false; // TC1 AIN2/AIN3
    if (!ad7124_config_channel(dev, 2, true, 0, 4, 5)) return false; // TC2 AIN4/AIN5

    // ----------------
    // Cold Junction RTD
    // RTD_CJ top at AIN6, return sensed at AIN_R
    // ----------------
    if (!ad7124_config_channel(dev, 3, true, 1, 6, AD7124_AIN_R)) return false;

    // ----------------
    // RTD0..RTD7
    // tops at AIN7..AIN14, return sensed at AIN_R
    // ----------------
    for (uint8_t i = 0; i < rtd_count; i++) {
        uint8_t ainp = (uint8_t)(7 + i);
        uint8_t ch = (uint8_t)(4 + i);
        if (ch > 15) break;
        if (!ad7124_config_channel(dev, ch, true, 1, ainp, AD7124_AIN_R)) return false;
    }

    return true;
}




// -------------------------------
// 1) Sanity check
// -------------------------------
//
// Expected conditions for your RTD topology:
// - DATA_STATUS enabled (recommended)
// - CONFIG_{rtd_setup}:
//    * REF_SEL = REFIN1
//    * unipolar
//    * PGA = 1
//    * AIN buffers ON (suggested default for your RC/100R node)
// - FILTER_{rtd_setup}: no strict value check on FS here unless you want to add it
// - CJ channel uses:
//    * setup = rtd_setup
//    * AINP = AIN6
//    * AINM = AIN_R
// - RTD0..7 channels use:
//    * setup = rtd_setup
//    * AINP = AIN7..AIN14
//    * AINM = AIN_R
//
bool ad7124_sanity_check_rtd(ad7124_dev_t *dev,
                             uint8_t expected_rtd_setup,
                             uint8_t expected_rtd_count,
                             ad7124_sanity_report_t *out)
{
    if (!dev || !out) return false;
    if (expected_rtd_setup > 7) return false;
    if (expected_rtd_count > AD7124_RTD_MAX) expected_rtd_count = AD7124_RTD_MAX;

    memset(out, 0, sizeof(*out));
    out->ok = true;

    // ADC_CONTROL
    if (!ad7124_read_reg16(dev, AD7124_REG_ADC_CONTROL, &out->adc_control)) return false;

    bool data_status = (out->adc_control & (1u << 10)) != 0;
    out->adc_control_ok = data_status;
    if (!out->adc_control_ok) out->ok = false;

    // CONFIG for RTD setup
    if (!ad7124_read_reg16(dev, AD7124_REG_CONFIG(expected_rtd_setup), &out->config_rtd)) return false;

    // Decode fields as per our earlier layout:
    // 11 Bipolar, 8 REF_BUFP, 7 REF_BUFM, 6 AIN_BUFP, 5 AIN_BUFM, 4:3 REF_SEL, 2:0 PGA
    bool bipolar = (out->config_rtd & (1u << 11)) != 0;
    uint8_t ref_sel = (uint8_t)((out->config_rtd >> 3) & 0x3);
    uint8_t pga = (uint8_t)(out->config_rtd & 0x7);
    bool ain_bufp = (out->config_rtd & (1u << 6)) != 0;
    bool ain_bufm = (out->config_rtd & (1u << 5)) != 0;

    out->setup_rtd_ok = (!bipolar) &&
                        (ref_sel == AD7124_REF_REFIN1) &&
                        (pga == AD7124_PGA_1) &&
                        (ain_bufp && ain_bufm);

    if (!out->setup_rtd_ok) out->ok = false;

    // FILTER for RTD setup (we'll just confirm it's readable and type isn't nonsense)
    if (!ad7124_read_reg24(dev, AD7124_REG_FILTER(expected_rtd_setup), &out->filter_rtd)) return false;

    uint8_t filt_type = (uint8_t)((out->filter_rtd >> 21) & 0x7);
    out->filter_rtd_ok = (filt_type <= 7);
    if (!out->filter_rtd_ok) out->ok = false;

    // CJ channel is CH3 by our convention
    if (!ad7124_read_reg16(dev, AD7124_REG_CHANNEL(3), &out->ch_cj)) return false;

    {
        bool en = (out->ch_cj & (1u << 15)) != 0;
        uint8_t setup = (uint8_t)((out->ch_cj >> 12) & 0x7);
        uint8_t ainp = (uint8_t)((out->ch_cj >> 5) & 0x1F);
        uint8_t ainm = (uint8_t)(out->ch_cj & 0x1F);

        out->ch_cj_ok = en &&
                        (setup == expected_rtd_setup) &&
                        (ainp == AD7124_AIN_RTD_CJ) &&
                        (ainm == AD7124_AIN_R);

        if (!out->ch_cj_ok) out->ok = false;
    }

    // RTD0..channels: start at CH4
    for (uint8_t i = 0; i < expected_rtd_count; i++) {
        uint8_t ch = (uint8_t)(4 + i);
        if (ch > 15) { out->ch_rtd_ok[i] = false; out->ok = false; break; }

        if (!ad7124_read_reg16(dev, AD7124_REG_CHANNEL(ch), &out->ch_rtd[i])) return false;

        bool en = (out->ch_rtd[i] & (1u << 15)) != 0;
        uint8_t setup = (uint8_t)((out->ch_rtd[i] >> 12) & 0x7);
        uint8_t ainp = (uint8_t)((out->ch_rtd[i] >> 5) & 0x1F);
        uint8_t ainm = (uint8_t)(out->ch_rtd[i] & 0x1F);

        uint8_t expected_ainp = (uint8_t)(AD7124_AIN_RTD0_BASE + i);

        out->ch_rtd_ok[i] = en &&
                            (setup == expected_rtd_setup) &&
                            (ainp == expected_ainp) &&
                            (ainm == AD7124_AIN_R);

        if (!out->ch_rtd_ok[i]) out->ok = false;
    }

    return true;
}

// -------------------------------
// 2) RTD scan function (continuous sequencing)
// -------------------------------
//
// This assumes you enabled channels as:
//   CH3 = CJ
//   CH4.. = RTD0..
// and ADC is in CONTINUOUS with DATA_STATUS.
//
bool ad7124_scan_rtds(ad7124_dev_t *dev,
                      uint8_t rtd_setup,
                      uint8_t rtd_count,
                      double r_ref_ohms,
                      uint32_t timeout_us,
                      ad7124_rtd_sample_t *cj_out,
                      ad7124_rtd_sample_t *rtd_out_array,
                      size_t rtd_out_array_len)
{
    if (!dev) return false;
    if (rtd_count > AD7124_RTD_MAX) rtd_count = AD7124_RTD_MAX;
    if (rtd_out_array && rtd_out_array_len < rtd_count) return false;

    // We'll collect CJ + each RTD once.
    bool got_cj = false;
    bool got_rtd[AD7124_RTD_MAX] = {0};

    // Prime outputs
    if (cj_out) memset(cj_out, 0, sizeof(*cj_out));
    if (rtd_out_array) memset(rtd_out_array, 0, sizeof(ad7124_rtd_sample_t) * rtd_count);

    // We will loop until all are captured or a global time budget is exceeded.
    absolute_time_t deadline = make_timeout_time_us(timeout_us * (uint32_t)(1 + rtd_count));

    while (!time_reached(deadline)) {
        if (!ad7124_wait_ready(dev, timeout_us)) continue;

        uint32_t code = 0;
        uint8_t ch_active = 0xFF;
        if (!ad7124_read_sample(dev, &code, &ch_active)) continue;

        if (ch_active == 3 && cj_out && !got_cj) {
            cj_out->logical_index = 0xFF; // mark as CJ
            cj_out->ad7124_channel = 3;
            cj_out->ainp = AD7124_AIN_RTD_CJ;
            cj_out->ainm = AD7124_AIN_R;
            cj_out->code = code;
            cj_out->resistance_ohms = ad7124_rtd_resistance_from_code_ratiometric(code, r_ref_ohms);
            cj_out->ok = true;
            got_cj = true;
        } else if (ch_active >= 4 && ch_active < (uint8_t)(4 + rtd_count) &&
                   rtd_out_array) {
            uint8_t i = (uint8_t)(ch_active - 4);
            if (!got_rtd[i]) {
                rtd_out_array[i].logical_index = i;
                rtd_out_array[i].ad7124_channel = ch_active;
                rtd_out_array[i].ainp = (uint8_t)(AD7124_AIN_RTD0_BASE + i);
                rtd_out_array[i].ainm = AD7124_AIN_R;
                rtd_out_array[i].code = code;
                rtd_out_array[i].resistance_ohms = ad7124_rtd_resistance_from_code_ratiometric(code, r_ref_ohms);
                rtd_out_array[i].ok = true;
                got_rtd[i] = true;
            }
        }

        // Check completion
        bool all = got_cj || (cj_out == NULL);
        for (uint8_t i = 0; i < rtd_count; i++) {
            if (rtd_out_array == NULL) break;
            all = all && got_rtd[i];
        }
        if (all) return true;
    }

    return false;
}

// -------------------------------
// 3) Safe IOUT strategy
// -------------------------------
//
// What makes it "safe":
// - Only ONE RTD is excited at a time.
// - Avoids unintended parallel current paths through multiple RTDs
//   sharing RTD_R.
// - Minimizes self-heating per sensor.
// - Prevents "ghost" measurement offsets due to current source
//   compliance changes under multi-load conditions.
// - Allows deterministic settle time per channel if you add a delay.
//
// This is especially important when the AD7124 IOUTs are used to
// drive multiple RTDs that share a common return, as in your topology.
//
bool ad7124_enable_safe_iout_for_rtd(ad7124_dev_t *dev,
                                     ad7124_iout_val_t iout,
                                     uint8_t rtd_top_ain,
                                     bool close_pdsw)
{
    // We use IOUT0 only; IOUT1 is OFF.
    return ad7124_set_iout(dev,
                           iout, rtd_top_ain,
                           AD7124_IOUT_OFF, 0,
                           close_pdsw);
}

bool ad7124_disable_iouts(ad7124_dev_t *dev)
{
    return ad7124_set_iout(dev,
                           AD7124_IOUT_OFF, 0,
                           AD7124_IOUT_OFF, 0,
                           false);
}

// High-level safe read of one RTD using SINGLE conversion mode.
//
// Steps:
// 1) Route IOUT to this RTD top only.
// 2) Enable only its channel (optional but recommended).
// 3) Switch to SINGLE mode.
// 4) Wait ready + read sample.
// 5) Disable IOUTs.
// 6) Restore CONTINUOUS mode (optional).
//
bool ad7124_read_one_rtd_safe(ad7124_dev_t *dev,
                              uint8_t ad7124_channel,
                              uint8_t rtd_setup,
                              ad7124_iout_val_t iout,
                              uint8_t rtd_top_ain,
                              double r_ref_ohms,
                              uint32_t timeout_us,
                              ad7124_rtd_sample_t *out)
{
    if (!dev || !out) return false;
    if (ad7124_channel > 15 || rtd_setup > 7) return false;

    memset(out, 0, sizeof(*out));

    if (!ad7124_enable_safe_iout_for_rtd(dev, iout, rtd_top_ain, false)) return false;

    for (uint8_t ch = 0; ch < 16; ch++) {
        if (ch == ad7124_channel) continue;
        (void)ad7124_config_channel(dev, ch, false, 0, 0, 1);
    }

    if (!ad7124_config_channel(dev, ad7124_channel, true, rtd_setup, rtd_top_ain, AD7124_AIN_R)) {
        (void)ad7124_disable_iouts(dev);
        return false;
    }

    if (!ad7124_set_mode_only(dev, AD7124_MODE_SINGLE)) {
        (void)ad7124_disable_iouts(dev);
        return false;
    }

    if (!ad7124_wait_ready(dev, timeout_us)) {
        (void)ad7124_disable_iouts(dev);
        return false;
    }

    uint32_t code = 0;
    uint8_t ch_active = 0xFF;
    if (!ad7124_read_sample(dev, &code, &ch_active)) {
        (void)ad7124_disable_iouts(dev);
        return false;
    }

    (void)ad7124_disable_iouts(dev);
    (void)ad7124_set_mode_only(dev, AD7124_MODE_CONTINUOUS);

    // Populate output
    uint8_t li = ad7124_logical_index_from_rtd_top_ain(rtd_top_ain);
    if (li == 0xFF) {
        li = ad7124_logical_index_from_channel(ad7124_channel);
    }

    out->logical_index = li;
    out->ad7124_channel = ad7124_channel;
    out->ainp = rtd_top_ain;
    out->ainm = AD7124_AIN_R;
    out->code = code;
    out->resistance_ohms = ad7124_rtd_resistance_from_code_ratiometric(code, r_ref_ohms);

    // With all other channels disabled + SINGLE mode, ch_active should match.
    // But keep your original logic:
    out->ok = (ch_active == ad7124_channel) || !dev->data_status_enabled;

    return out->ok;
}
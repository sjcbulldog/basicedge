# TLV320DAC3100 Audio Codec — Library Integration

**Applies to:** Boards with an on-board TLV320DAC3100 stereo audio DAC chip (e.g., KIT_PSE84_EVAL_EPC2). Not applicable to designs without this DAC present.

⛔ **FORBIDDEN:** Writing raw TLV320DAC3100 register sequences. Use the `mtb_tlv320dac3100` library (3 function calls replace 50+ register writes).

**Reference CE:** `mtb-example-psoc-edge-i2s`

## Quick Reference

| Item | Value |
|------|-------|
| Library dependency | `mtb://tlv320dac3100/latest-v1.X` |
| I2C address | `0x18` |
| I2C bus | `CYBSP_I2C_CONTROLLER_HW` (SCB0, 400 kHz) |
| Audio interface | I2S via TDM0 (see [audio-pdm-i2s-patterns.md](../../mtb-device-configurator/references/audio-pdm-i2s-patterns.md)) |
| MCLK source | TDM0 MCK output pin |
| Required init order | `Cy_AudioTDM_Init()` → `Cy_AudioTDM_EnableTx()` → 1 ms delay → `mtb_tlv320dac3100_init()` → `mtb_tlv320dac3100_configure_clocking()` → `mtb_tlv320dac3100_activate()` |
| Key rule | TDM MCK must already be outputting before codec init |
| Clocking rule | Pass the real MCLK Hz to `configure_clocking()`; the library handles PLL/divider math internally |
| Output options | `TLV320DAC3100_SPK_AUDIO_OUTPUT`, `TLV320DAC3100_HP_AUDIO_OUTPUT` |
| Sample rate enums | 8, 16, 32, 44.1, 48 kHz |

## ⛔ Gotchas

| # | Gotcha | Impact | Fix |
|---|--------|--------|-----|
| G1 | Raw register writes instead of library | Wrong PLL sequence, no audio | Always use `mtb_tlv320dac3100_*` functions |
| G2 | MCLK Hz mismatch between TDM config and `configure_clocking()` | PLL won't lock, silence | Verify Device Configurator TDM clock matches code parameter |
| G3 | Calling `activate()` before `configure_clocking()` | Codec runs with default/wrong config | Always configure clocking first |
| G4 | I2C not enabled before codec init | NAK on all transactions | `Cy_SCB_I2C_Enable()` must precede `mtb_tlv320dac3100_init()` |
| G5 | Using raw PDL I2C without HAL wrapper | Codec library expects `mtb_hal_i2c_t` | Must create HAL object via `mtb_hal_i2c_setup()` |
| G6 | Codec init before TDM `EnableTx` | MCK not outputting, codec gets no clock input | Enable TDM first, delay 1ms, then codec init |
| G7 | Manual PLL calculation | Library handles ALL clock math internally | Just pass correct MCLK Hz to `configure_clocking()` |
| G8 | Assuming MCLK from user's stated sample rate | Actual BSP clock yields different frequency | Read `cycfg_peripheral_clocks.c` for real divider values |

## Init Code

⚠️ **The TDM peripheral MUST be enabled (MCK outputting) BEFORE calling `mtb_tlv320dac3100_init()`.** The codec requires MCLK present on its input pin to accept configuration. If you init the codec before enabling TDM, it receives no clock and register writes may succeed (ACK) but the codec will not function.

```
Cy_AudioTDM_Init(...)        ← TDM block configured
Cy_AudioTDM_EnableTx(...)    ← MCK starts outputting on pin
    ↓ (1ms delay recommended)
mtb_tlv320dac3100_init(...)   ← Codec can now receive clock
mtb_tlv320dac3100_configure_clocking(...)
mtb_tlv320dac3100_activate()
```

The codec library requires a HAL I2C object (not raw PDL). Init order:

```c
#include "mtb_tlv320dac3100.h"
#include "mtb_hal.h"
#include "cy_scb_i2c.h"

/* Persistent objects */
mtb_hal_i2c_t codec_i2c_obj;
cy_stc_scb_i2c_context_t codec_i2c_ctx;

const mtb_hal_i2c_cfg_t i2c_cfg = {
    .is_target = false,
    .address = 0x18,
    .frequency_hz = 400000u,
    .address_mask = MTB_HAL_I2C_DEFAULT_ADDR_MASK,
    .enable_address_callback = false
};

void codec_init(void)
{
    /* 1. PDL I2C init (BSP-generated config) */
    Cy_SCB_I2C_Init(CYBSP_I2C_CONTROLLER_HW, &CYBSP_I2C_CONTROLLER_config, &codec_i2c_ctx);
    Cy_SCB_I2C_Enable(CYBSP_I2C_CONTROLLER_HW);

    /* 2. HAL I2C wrapper (codec library requires HAL object) */
    mtb_hal_i2c_setup(&codec_i2c_obj, &CYBSP_I2C_CONTROLLER_hal_config, &codec_i2c_ctx, NULL);
    mtb_hal_i2c_configure(&codec_i2c_obj, &i2c_cfg);

    /* 3. Codec init — resets device, sets up I2C communication */
    mtb_tlv320dac3100_init(&codec_i2c_obj);

    /* 4. Configure clocking — library calculates PLL/NDAC/MDAC internally */
    mtb_tlv320dac3100_configure_clocking(
        2048000u,                              /* MCLK Hz — MUST match TDM Device Configurator config */
        TLV320DAC3100_DAC_SAMPLE_RATE_16_KHZ,  /* Sample rate */
        TLV320DAC3100_I2S_WORD_SIZE_16,        /* Word length */
        TLV320DAC3100_SPK_AUDIO_OUTPUT         /* Output: SPK or HP */
    );

    /* 5. Activate — powers DAC, unmutes, enables output drivers */
    mtb_tlv320dac3100_activate();
}
```

## DC Settings

| Setting | Value |
|---------|-------|
| I2C controller | `CYBSP_I2C_CONTROLLER_HW` |
| I2C block | SCB0 |
| I2C frequency | 400 kHz |
| Codec I2C address | `0x18` |
| Audio data path | I2S via TDM0 |
| Master clock source | TDM0 MCK output pin |

The MCLK frequency passed to `configure_clocking()` **MUST** match the TDM master clock configured in Device Configurator. Common pairs:

| Sample Rate | MCLK (×128) | MCLK (×256) |
|-------------|-------------|-------------|
| 16 kHz | 2.048 MHz | 4.096 MHz |
| 44.1 kHz | 5.6448 MHz | 11.2896 MHz |
| 48 kHz | 6.144 MHz | 12.288 MHz |

## Detailed Guidance

### Hardware Connection (KIT_PSE84_EVAL_EPC2)

- **I2C address:** `0x18`
- **I2C bus:** `CYBSP_I2C_CONTROLLER_HW` (SCB0, 400 kHz)
- **Audio interface:** I2S via TDM0 (see [audio-pdm-i2s-patterns.md](../../mtb-device-configurator/references/audio-pdm-i2s-patterns.md))
- **MCLK:** Provided by TDM0 MCK output pin

### Library Dependency

Add via Library Manager or `.mtb` file:
```
mtb://tlv320dac3100/latest-v1.X
```

**Key fact:** The `mtb_tlv320dac3100` library handles ALL clock math internally — PLL configuration, NDAC/MDAC dividers, DAC_MOD_CLK calculation. You provide MCLK frequency and desired sample rate; the library computes the rest. **Do NOT manually calculate PLL register values.**

### Library API

| Function | Purpose |
|----------|---------|
| `mtb_tlv320dac3100_init(i2c_obj)` | Reset codec, establish I2C |
| `mtb_tlv320dac3100_configure_clocking(mclk, rate, word, output)` | PLL + divider + routing |
| `mtb_tlv320dac3100_activate()` | Power up DAC, unmute |
| `mtb_tlv320dac3100_deactivate()` | Mute, power down |
| `mtb_tlv320dac3100_set_volume(vol)` | DAC volume control |

### Sample Rate Options

| Enum | Rate |
|------|------|
| `TLV320DAC3100_DAC_SAMPLE_RATE_8_KHZ` | 8 kHz |
| `TLV320DAC3100_DAC_SAMPLE_RATE_16_KHZ` | 16 kHz |
| `TLV320DAC3100_DAC_SAMPLE_RATE_32_KHZ` | 32 kHz |
| `TLV320DAC3100_DAC_SAMPLE_RATE_44_1_KHZ` | 44.1 kHz |
| `TLV320DAC3100_DAC_SAMPLE_RATE_48_KHZ` | 48 kHz |

### Output Routing

| Enum | Path |
|------|------|
| `TLV320DAC3100_SPK_AUDIO_OUTPUT` | On-board speaker amplifier |
| `TLV320DAC3100_HP_AUDIO_OUTPUT` | Headphone output driver |

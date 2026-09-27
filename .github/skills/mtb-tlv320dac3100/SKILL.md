---
name: mtb-tlv320dac3100
description: Integrate the TLV320DAC3100 audio codec library on boards with an on-board codec (e.g., KIT_PSE84_EVAL_EPC2). Use when asked to play audio, set up I2S audio output, configure the DAC codec, or integrate the TLV320DAC3100 library.
license: "Apache-2.0"
metadata:
  author: "Infineon ModusToolbox Team"
  version: "1.1.0"
---

# TLV320DAC3100 Audio Codec — Middleware Library

## Quick Reference

| Item | Value |
|------|-------|
| **Library** | `mtb://tlv320dac3100/latest-v1.X` |
| **Type** | Middleware (wraps I2C register access for audio codec) |
| **Board** | KIT_PSE84_EVAL_EPC2 (on-board codec) — not applicable to boards without this codec |
| **I2C address** | `0x18` |
| **Audio interface** | I2S via TDM peripheral |
| **Key rule** | TDM MCK must be outputting BEFORE codec init |

## ⛔ Gotchas (Read First)

| # | Gotcha | Impact |
|---|--------|--------|
| G1 | Raw register writes instead of library | Wrong PLL sequence, no audio — always use `mtb_tlv320dac3100_*` functions |
| G2 | MCLK Hz mismatch between TDM config and `configure_clocking()` | PLL won't lock, silence |
| G3 | Calling `activate()` before `configure_clocking()` | Codec runs with wrong config |
| G4 | I2C not enabled before codec init | NAK on all transactions |
| G5 | Codec library expects `mtb_hal_i2c_t` (not raw PDL) | Must create HAL object via `mtb_hal_i2c_setup()` |
| G6 | Codec init before TDM `EnableTx` | MCK not outputting, codec gets no clock |

## Prerequisites — Device Configurator

Before integrating this library, the following peripherals must be configured in Device Configurator. Invoke the `mtb-device-configurator` skill with:

```yaml
peripherals:
  - type: I2C
    alias: CYBSP_I2C_CONTROLLER
    settings:
      mode: Master
      frequency: 400kHz
  - type: TDM
    alias: TDM0
    settings:
      direction: TX
      word_size: 16-bit
      channels: 2 (stereo)
      master_clock: enabled
```

## Workflow

1. **Add library dependency** — `mtb://tlv320dac3100/latest-v1.X` then `make getlibs`
2. **Configure peripherals** — invoke `mtb-device-configurator` for I2C + TDM (see Prerequisites above)
3. **Init code** — see [integration-guide.md](./references/integration-guide.md)
4. **Build:** `make -j build`

## References

- [Full Integration Guide](./references/integration-guide.md) — init order, code, DC settings, hardware details

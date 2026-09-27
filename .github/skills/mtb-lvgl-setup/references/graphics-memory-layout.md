# Step 1: Memory Configurator — Rebalance for Graphics

## Quick Reference

| Item | Value |
|------|-------|
| **Tool** | Memory Configurator tab in `bsps/TARGET_*/config/design.modus` |
| **Key region** | `gfx_mem` — minimum 3 MB in SOCMEM |
| **App must verify** | Clean build with no linker overflow (see §Verification below) |
| **Common trap** | Over-allocating SOCMEM (total budget = 5 MB) |

## ⛔ Gotchas (Read First)

| # | Gotcha | Impact |
|---|--------|--------|
| G1 | Over-allocating SOCMEM | Linker "region overflow" error — you only have 5 MB total |
| G2 | Forgetting `m33_m55_shared` for IPC | Cores can't communicate — add 256 KB if using IPC |
| G3 | Shrink-before-grow rule | Memory Configurator validates intermediate states — shrink first, then add |

---

Graphics requires ~3 MB of SOCMEM for framebuffers and GPU heap. The existing project likely doesn't allocate this.

## Required Regions

| Region | Location | Minimum Size | Purpose |
|--------|----------|-------------|---------|
| `m33_code` | Flash | Per existing project | CM33 application code |
| `m33_data` | SRAM | ≥520 KB for Matter/WiFi | CM33 runtime (heap, stacks, buffers) |
| `m55_code_secondary` | SOCMEM | 384 KB | CM55 code overflow (LVGL, drivers) |
| `m55_data_secondary` | SOCMEM | 1408 KB | CM55 heap, LVGL fonts, buffers |
| `gfx_mem` | SOCMEM | 3072 KB (3 MB) | 2× framebuffers + VG-Lite GPU heap |
| `m33_m55_shared` | SOCMEM | 256 KB | IPC shared memory (if needed) |

## Key Rules

| Rule | Why |
|------|-----|
| Addresses are auto-calculated | Only specify sizes; the tool handles offsets |
| Rebuild ALL three projects | CM33_S, CM33_NS, CM55 — linker scripts are per-project |
| Verify existing features still work | After memory rebalance, before adding any graphics code |

---

## Step-by-Step

1. **Open Memory Configurator** — File → Open → `bsps/TARGET_*/config/design.modus` → Memory tab

2. **Note current allocations** — Screenshot or note existing sizes before changes

3. **Shrink before growing** — Memory Configurator validates intermediate states. If you need to add `gfx_mem`, first shrink an existing region to make space.

4. **Add/resize `gfx_mem` region:**
   - Location: SOCMEM
   - Size: 3145728 (3 MB)
   - Purpose: Graphics framebuffers + GPU heap

5. **Verify `m55_data_secondary`** has at least 1408 KB for LVGL heap

6. **If using IPC**, add `m33_m55_shared` region (256 KB in SOCMEM)

7. **Save and regenerate** — Device Configurator regenerates linker scripts

## Validation

After memory changes, build all projects and verify no linker errors:

```
python .github/skills/mtb-tools/scripts/run_make.py --args clean
python .github/skills/mtb-tools/scripts/run_make.py --args build
```

If you see "region overflow" errors, you've over-allocated. Reduce a region size.

---

**Next:** [gfxss-display-config.md](./gfxss-display-config.md) — Device Configurator GFXSS personality

# Signal Function Set — MetaModule Port

MetaModule plugin port of [Signal Function Set](https://github.com/stuart78/SignalFunctionSet) for VCV Rack.

## Modules

- **Drift** — Phase-shifted LFO with Lorenz attractor chaos
- **GSX** — Granular synthesis (Barry Truax GSX system)
- **Fugue** — 8-step harmonic deviation sequencer
- **Phase** — Dual sample looper with phase drift
- **Overtone** — Additive synthesis VCO with 8 harmonics
- **Intone** — CHANT/FOF formant synthesis voice

## Building

### Prerequisites

- CMake v3.22+
- ARM GCC toolchain v12.2 or v12.3
- Inkscape v1.2.2+ (for SVG→PNG panel conversion)

### Setup

```bash
# Clone the MetaModule SDK (v2.1+)
git clone https://github.com/4ms/metamodule-plugin-sdk.git

# Convert SVG panels to PNG
./scripts/convert-panels.sh

# Build
cmake -B build -G Ninja -DMETAMODULE_SDK_DIR=./metamodule-plugin-sdk
cmake --build build
```

The output `.mmplugin` file will be in `metamodule-plugins/`.

### Panel PNG Conversion

The SVG source panels are in `assets/panels/`. Run `scripts/convert-panels.sh` to generate the PNGs that MetaModule requires. You need Inkscape installed.

## Differences from VCV Rack version

- Panel graphics are PNG (240px height) instead of SVG
- Phase module uses MetaModule async file browser instead of osdialog
- NanoVG custom displays (Phase, Overtone, Intone) render at reduced framerate

# Layout Mode OBS Bypass

A Geode mod for Geometry Dash 2.2081 that lets you play with **Layout Mode** on your screen while **OBS captures normal gameplay** via Spout2.

## How It Works

1. **Screen** → Layout Mode (decorations hidden, simplified colors)
2. **OBS** → Normal gameplay via Spout2 shared texture (GPU-to-GPU, minimal overhead)
3. **HUD/Indicators** → Untouched (CPS counters, labels, etc. appear on both)

## Setup

### Prerequisites
- [Geode](https://geode-sdk.org/) installed
- [obs-spout2-plugin](https://github.com/Off-World-Live/obs-spout2-plugin) for OBS

### Install
1. Download the `.geode` file from [Releases](../../releases)
2. Put it in your Geode mods folder
3. In OBS: Add source → **Spout2 Capture** → Select **"GD_Clean"**

### Build from source
```bash
# Clone with submodules (for Spout2)
git clone --recursive https://github.com/YOUR_NAME/layout-obs-bypass.git
cd layout-obs-bypass

# Build with Geode CLI
geode build
```

Or set `GEODE_SDK` env variable and use CMake directly:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Settings
- **Enable Dual Render** — Toggle the mod on/off
- **Spout Sender Name** — Name shown in OBS Spout2 Capture (default: "GD_Clean")

## Spout2 SDK
Spout2 is pulled automatically at configure time via CMake `FetchContent`
from https://github.com/leadedge/Spout2. No manual download or `libs/SpoutGL/`
folder is needed — just configure and build.

## License
MIT

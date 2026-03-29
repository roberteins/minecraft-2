# minecraft-2

A lightweight Minecraft-style voxel prototype in C++ using OpenGL + freeglut.

## Features

- 3D voxel terrain with grass/dirt/stone stratification.
- First-person movement with gravity, jump, and sprint.
- Minecraft-like block interaction:
  - `LMB` break block.
  - `RMB` place selected block.
- 9-slot hotbar (`1`..`9`) with textured icons.
- Basic block set: grass, dirt, stone, planks, cobblestone, sand, glass, oak log, bricks.
- Uses Minecraft-style textures from a local assets directory when available.

## Controls

- `WASD`: move
- `Mouse`: look
- `Space`: jump
- `Tab`: sprint
- `1..9`: select hotbar slot
- `Esc`: quit

## Build (Ubuntu 24.04)

Install dependencies:

```bash
sudo apt-get update
sudo apt-get install -y g++ freeglut3-dev libgl1-mesa-dev libglu1-mesa-dev libpng-dev
```

Compile:

```bash
make
```

Run:

```bash
./minecraft-2
```

## Assets

The executable looks for textures in this order:

1. `$MC_ASSET_ROOT/textures/block/*.png`
2. `/src/textures/block/*.png`
3. `./src/textures/block/*.png`

Example using your local extracted Minecraft client assets:

```bash
MC_ASSET_ROOT=/path/to/assets ./minecraft-2
```

If a texture is missing, a generated fallback checker texture is used.

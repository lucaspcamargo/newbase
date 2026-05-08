Sonic-style quad-mode physics implemented entirely in **Lua**, using raycasting for all sensor logic.

The player can run on **floors, walls, and ceilings** using momentum, slope factors, and speed thresholds. Ground sensors (A/B), ceiling sensors (C/D), and wall sensors (E/F) are implemented via `physics2d_raycast`.

Use **arrow keys** to move and **`Z`** (joypad A) to jump. Build up speed to stick to walls and run upside-down.

Press **`F1`** to bring up the editor tools to inspect the scene and entity components.

### Assets

- **Sprite:** Sonic CD character sprite, property of Sega/Sonic Team — upscaled 4× via the xBR algorithm. Used for demonstration purposes only.

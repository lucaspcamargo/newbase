Sonic-style quad-mode physics implemented entirely in **Lua**, using raycasting for all sensor logic.

The player can run on **floors, walls, and ceilings** using momentum, slope factors, and speed thresholds. Ground sensors (A/B), ceiling sensors (C/D), and wall sensors (E/F) are implemented via `physics2d_raycast`.

Use **arrow keys** to move and **[Space]** to jump. Build up speed to stick to walls and run upside-down.

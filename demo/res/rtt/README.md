A simple Render-to-Texture test.

Two render targets are created, the scene buffer and accumulation buffer. The scene is rendered normally on the scene buffer. This texture is then drawn to the accumulation buffer with a low opacity, every frame, creating a temporal motion blur effect.

The accumulation buffer is then rendered to the screen as a regular texture.

This was used to validate the render target size dependency graph and scaling modes, and shows a simple effect, without using any shaders.

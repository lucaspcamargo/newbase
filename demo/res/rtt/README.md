A simple Render-to-Texture test.

Two render targets are created, the scene buffer and accumulation buffer. The scene is renderer normally on the scene buffer. This txture is then drawn to the accumulation buffer with a low opacity, repeatedly, creating a temporal motion blur effect.

The accumulation buffer is then rendered to the screen as a regular texture.

This was used to validate the render target size dependency graph and scaling modes, and shows a simple effect, without using any shaders. The texture is made smaller than the viewport pixel size on purpose, and not filtered, to test automatic viewport-dependent target scaling.

# TODO

The things that are still meant to be done. Some have 

- Core
    + [ ] MULTISCENE. This will be a big one.
    + [ ] RTTI: generalized serialization and deserialization system (yaml/meta_any gets close)
    + [ ] RTTI: enums support
    + [ ] Use entt's new support for names associated with members and functions, reducing usage of custom data.
    + [ ] Remove system-specific resources and components from core
    + [ ] Decouple rtexture from SDL_Texture. Texture specialization is a renderer task.
- Editor
    + [ ] Scene editing and loading
    + [ ] Multiple scenes in separate tabs
- Rendering
    * [~] Generalize 2D rendering
    + [ ] Make render_simple use generalized 2D rendering
    + [ ] Blend modes
    + [ ] Get render_gpu into a basic working state. Depends on the above rendering tasks.
    + [ ] Shader system
    + [ ] Materials system + shader derivation
    + [ ] "Classic" fixed-function-like materials system
    + [ ] PBR material system
    + [ ] GLTF import and rendering
- Audio
    + [ ] "Maestro" system for handling dynamic music and transitions
    + [ ] Spatial sound support, 2D and 3D
    + [ ] Basic HRTF support in spatial functionality
- In-Game UI
    + [ ] Integrate RmlUi
    + [ ] Integrate LVGL
    + [ ] Integrate Nuklear
- Lua Scripting
    + [ ] Mechanism for automatic callback cleanup
    + [ ] Wire up enum RTTI when they are in place
- Lupi
    + [ ] Music and sfx, when Lupinho has it too and it is better documented
- Sensors
    + [ ] Add Android-specific sensor handling
- SGDK
    + [ ] Get it working for god's sake
- QOL
    + [ ] Add subdirectory for system headers and sources (newbase system)
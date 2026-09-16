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
    + [ ] Scene editing: loading from etree, saving to etree
    + [ ] Multiple scenes in separate tabs
- Resources
    + [ ] Load jobs
        + [ ] Resource dependencies in components and other resources(scene->cscript->rscript->rtexture, for example)
    + [ ] Background loading
    + [ ] Organize loaders with resource types
- Rendering
    * [X] Generalize 2D rendering
    + [X] Shared window code
    + [X] Make render_simple use generalized 2D rendering
    + [~] Blend modes in sprites and textures, like in cgeom2d (support in 2d renderer done)
    + [ ] Implement ImGui backend specific to newbase, stop using the sample code.
    + [ ] Rewrite render_gpu to use the new stuff and get 2D working right.
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
    + [ ] Change demo to have in-game UI. Pick which one is best for the job.
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
    + [X] Organize newbase systems in subdirectories (newbase system)
    + [ ] Group system-specific components and resources with their systems

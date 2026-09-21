# TODO

The things that are still meant to be done. Some have 

- Core
    + [ ] MULTISCENE. This will be a big refactor.
    + [ ] RTTI, scene: generalized serialization and deserialization system (yaml/meta_any gets close)
    + [ ] RTTI: enums support
    + [ ] Use entt's new support for names associated with members and functions, reducing usage of custom data.
    + [ ] Move system-specific resources and components out of engine core
    + [X] Decouple rtexture from SDL_Texture. Texture specialization is a renderer task.
    + [ ] Ship standard "engine intro" scene in core resources
- Editor
    + [ ] Scene editing: loading from etree, saving to etree
    + [ ] Multiple scenes in separate tabs
- Resources
    + [ ] Load jobs
        + [ ] Resource dependencies in components and other resources(scene->cscript->rscript->rtexture, for example)
    + [ ] Background loading
    + [ ] Organize loaders and resource types together, split loaders TU
    + [ ] HTTP(S) resource provider, for emscripten mostly
    + [ ] Archive resource provider, for resource packing (WAD-like)
    + [ ] Support for resource providers to override each other in vfs with priorities (for mods and stuff)
- Rendering
    * [X] Generalize 2D rendering
    + [X] Shared window code
    + [X] Make render_simple use generalized 2D rendering
    + [X] Blend modes in sprites like in cgeom2d (support in 2d renderer done)
    + [~] Implement ImGui backend specific to newbase, stop using the sample code.
        * [X] Get basic support working
        * [X] Specify and implement clipping behavior
        * [ ] Validate (and fix?) DPI handling on all platforms
    + [X] Clipping on render_2d (and ImGui)
    + [~] Cleanup render layer and viewport semantics, and create spec - see below section
    * [ ] Basic render-to-texture with render layers
    + [X] Don't leak SDL_Texture on resource cleanup.
    + [ ] Rewrite render_gpu to use the new stuff and get 2D working right for a start.
    + [ ] Shader system
        * [ ] Optional shader support in render_2d depending on backend
        * [ ] Shader support in render_gpu for 2d
    + [ ] Materials system + shader derivation
    + [ ] "Classic" fixed-function-like materials system
    + [ ] PBR material system
    + [ ] GLTF import and rendering
    + [ ] Fancier techniques
        * [ ] Shadow mapping
        * [ ] HDR and Tonemapping
        * [ ] SSAO, SSR 
- Audio
    + [ ] Spatial sound support, 2D and 3D
    + [ ] Basic HRTF support in spatial functionality
    + [ ] "Maestro" system for handling dynamic music and transitions
- Tooling UI (ImGui)
    + [ ] Drop ImTextureRef being SDL_Texture, change to rtexture::id instead
        * To avoid constant texture reloads, we may cache loaded textures until the next frame.
          Mostly a contingency, as UI that renders with textures should itself hold references to the textures it uses.
    + [ ] Unify tool window toggle, remove dedicated debug actions
    + [ ] Default dock layout mechanism for tool and editor windows
    + [ ] Integrate imgui-text-editor for scripts and text files
- In-Game UI
    + [ ] Integrate LVGL
    + [ ] Integrate RmlUi
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
- Demo
    + [ ] Proper in-game UI
    + [ ] Asteroids: add proper game loop and dynamic object spawning in waves
    + [ ] Platformer: A bit more polish, no need to go overboard though
    + [ ] Fast-rodent: Get it working with some cool resources from the web
    + [ ] Physics 2D: Fix picking on web and Android, audio and more stuff
    + [X] Move hello-world to the end
    
    
    
## Scope: Viewports, Layers, and 2D Camera refactor

- [X] Remove viewports from renderer_service, and vp handle concept
    + [X] Update render_layer to contain viewport directly
- [X] Remove clear color from renderer_service
- [X] Implement UI viewport update logic
- [X] New camera code
    + [X] camera_2d structure in render namespace
    + [X] helpers to get camera_2d world bounds according to cam data and viewport
    + [X] update camera component and builder to contain new structure (2d and 3d data side by side is fine)
- [X] Remove fallback camera and viewport path from render_2d
- [~] Remove get_2d_extents and cam_2d_setup from renderer_service too
- [ ] Refine overlay API
- [ ] Update UI overlay callback signature and code

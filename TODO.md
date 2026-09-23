# TODO

The tasks that are still meant to be done. Some have dependencies or subtasks.

This is a living document.

- Core
    + [ ] MULTISCENE. This will be a big refactor.
    + [ ] RTTI, scene: generalized serialization and deserialization system (yaml/meta_any gets close)
    + [ ] RTTI: enums support
    + [ ] Port to entt v4. Quite a bit of work, especially in script_lua. REQUIRES C++20.
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
    + [X] Cleanup render layer and viewport semantics, and create spec - see below section
    * [~] Basic render-to-texture with render layers - WIP, see below
    + [X] Don't leak SDL_Texture on resource cleanup.
    + [ ] Impove UI overlay interface and semantics - see below
    + [ ] Rewrite render_gpu to use the new stuff and get 2D working right for a start.
    + [ ] Elementary 3D rendering with render_gpu
        * [ ] Do some research and come up with MVP requirements
    + [ ] Shader system
        * [ ] Optional shader support in render_2d depending on backend
        * [ ] Shader support in render_gpu for 2d too
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
- [X] Remove get_2d_extents and cam_2d_setup from renderer_service too
  

## Scope: Render-To-Texture (RTT)

- [X] Define main interfaces
- [X] Implement render_2d internal structures
- [~] Implement target and viewport sizing mechanism
    + [X] RT sizing dependency graph ordering and evaluation.
    + [ ] Allow sizing viewports by RT, after RT resize pass (_targets_resize). Use a flag in layer.
- [ ] Implement render targeting proper
- [ ] Expose API to RTTI and script systems. Use target_ref in shared_ptr and a service locator.
- [ ] create simple demo
  
## Scope: UI Overlays
    
- [ ] Refine overlay API
        Right now, overlay callback takes no arguments and provides no context.
        That's fine but in case of, for example, physics overlay, it may have no
        idea of what camera to use. Perhaps tie overllay renndering to render layers.
        We could have a bool flag has_overlays and make ui_manager traverse it, 
        perhaps? Providing a render_layer should give the overlay enough context.
        If/when RTT is being used is another story in this scenario.
- [ ] Update UI overlay callback signature and code

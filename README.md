# newbase engine

![icon](res/_nb_core/icon.svg)


<!-- repo not yet public, may be in the future, then badge can be added : ![linux build badge](https://gt.camargo.eng.br/camargo/newbase/actions/workflows/linux_build.yaml/badge.svg?branch=main&event=push)
![emscripten build badge](https://gt.camargo.eng.br/camargo/newbase/actions/workflows/emscripten_build.yaml/badge.svg?branch=main&event=push)-->

A *new base* for making games and interactive stuff. Under heavy construction.

> “...maybe the real treasure was all the engines we made along the way!”
> -- Bob Nystrom

<!--ENGBR_SECTION
## Demo

If a picture is worth a thousand words, can you imagine what a demo is worth? Well, let's see for ourselves:

```{emscripten} newbase_emscripten_*.zip
    :entry: demo/newbase_demo.js
    :caption: $FILENAME_NO_EXT$ ($DATE$)
    :aspect-ratio: 16/9
    :idbfs-mount: /libsdl
```

*Nice. (I hope? Please let me know is something is amiss. Works on my machine :)*
ENGBR_SECTION-->

## Features

Target feature set for the first release is still in flux.

For now, we have:
- Data-driven Entity-Component-System architecture
- Integrated editor (WIP)
- Lua scripting
- SDL\_Renderer for basic 2D (uses appropriate GPU-accelerated render solution depending on platform)
- Graph-based audio playback and processing engine
- 2D Physics and collision handling
- Particle system
- Resource management
- RTTI system
- Some extras: tilemaps, textured text, 

## Platforms

The engine supports the following platforms:

- Linux (native and Flatpak builds)
- Emscripten (WASM)
- Windows (amd64)
- Android (aarch64)
- Nintendo Wii
    + preliminary version using [this SDL3 port](https://github.com/lucaspcamargo/SDL3-libogc2), atop devkitPPC and libogc2.

macOS should be relatively easy to add, once I actually have the necessary hardware and environment.
An attempt at a hackintosh VM did not work out. At this point, I'd rather have a native macOS
build machine when time and money allow.

It should be possible to build for other platforms and architectures, but this it is neither tested nor supported.

## ECS

Like many custom engines of today, `newbase` uses a data-driven ECS (Entity-Component-System) solution. This is based on the `entt` library to provide the optimized data structures.

This approach aims to split content, state, and logic as much as possible. The basic idea is to represent the game world as a set of identifiers (entities) that may have arbitrary sets of data associated with them. Every piece of data is a component. This data is ideally "pure" (a POD datatype in C++ parlance), without associated logic. The logic is provided by systems, operating over the entities and associated components.

The component data is stored together with other data of the same type, aiming to maximize data locality when systems operate on components in bulk. Updating transforms is a classic example of such usage.

There are many articles and primers around the web written about ECSs, if that is of interest. The [entt wiki](https://github.com/skypjack/entt/wiki) is a good start. This implementaion is a "pure" ECS, not requiring entities to have any specific data associated with it, besides their identifiers.

## Systems

Functionality is grouped in systems, that can be linked to the final executable as per its configuration. Systems work cooperatively to process the entities' component data and provide functionality used by the applications.

Here is a high-level description of the currently-implemented systems:

### render_simple

This is a simple video rendering system based on SDL3's Render system. It can draw textured sprites and geometry, with color modulation, in 2D. Its relative simplicity, from relying on SDL3's renderers, means it can work atop all of SDL3's render backends: Vulkan, OpenGL \[ES\], DX, Metal, software, all of them. This ensures that whatever platform is targeted, we can rely on SDL for basic 2D rendering support, with proper hardware acceleration in all major platforms.

ImGui is supported for debug and tooling (as it should be when using any render system of the engine).

### audio

The audio system is custom-made for the engine. It is based on an acyclic directed processing graph, that processes data in a pull fashion, generating samples as requested. Feedback is not directly supported by the graph, but can be used internally by the processing nodes when required (e.g. echo and reverb).

Audio sources can use in-memory buffers, or be streamed. In both cases they can be looped, and arbitrary loop points are supported.

There is a simple api (with Lua bindings) for playing sound effects and background music, just by specifying the resources to use. Simpler games can use it to play sound the easiest possible way. When using this API, the audio graph is managed automatically.

More complex games can use the audio graph directly, to chain effects and do more advanced processing. This requires usage of the C++ API.

See [the audio system documentation](doc/system_audio) for more detailed info.

### input

The base input system is a basic layer atop SDL's keyboard and joypad. It maps possible controller and keyboard
inputs to defined player actions.

Mouse input and multiple players are not yet supported, but is planned for the future.

### script_lua

This system provides Lua scripting support for the engine, written from the bottom-up using our RTTI system.

The initial idea was to allow for different scripting engines to be implementable, and this is still doable if a project requires it. But Lua has shown itself to be capable and performant enough. So it remains the one and only scripting interface supported by the engine.

### physics2d

This system implements 2D rigid-body physics, via the industry-standard Box2D library.

(TODO add more details)

<!--ENGBR_TOGGLE-->

### sgdk

The "sgdk" system is a bit of a black sheep. It does not interoperate with the ECS system as much. It is instead, a compatibility layer through which games written for the [SGDK](https://github.com/Stephane-D/SGDK) (Sega Genesis Development Kit) can work within newbase. It uses the engine's structures to consume input, and present video and audio. 

Games are compiled to native code, and no CPUs from the original system are emulated. The VDP is emulated in software, but can support different output resolutions, and other goodies. Therefore, a game may use some additional features when using the compatibility layer, and mix-and-match code meant for the Mega Drive and for newbase. This allows for (possibly enhanced) ports to be published for whatever platform is supported by newbase, without much compromise.

<!--ENGBR_TOGGLE-->

### spatial, clock

These are core systems of the engine, that implement standard data flows that other systems rely upon. The engine can run without them, but otherwise expected functionality may not work correctly.

## Dependencies

`newbase` aims to be as portable as possible, and not reinvent the wheel. Therefore, we leverage the following:

- [SDL3](https://libsdl.org/) - Main platform abstraction library, with many facilities for games 
- [entt](https://github.com/skypjack/entt) - Data structures and utilities for ECS systems
- [glm](https://github.com/g-truc/glm) - 3D math library
- [rapidyaml](https://github.com/biojppm/rapidyaml) - A fast and complete YAML library
- [lua](https://www.lua.org/) - The embeddable scripting language and runtime 
    + (it may be possible to use [LuaJIT](https://luajit.org/) for even faster speeds, but this is not yet tested)
- [sol3](https://github.com/ThePhD/sol2) - A fast, metaprogrammed Lua <-> C++ bindings library
- [ImGui](https://github.com/ocornut/imgui) - A lean C++ GUI library with flexible platform support
- [stb](https://github.com/nothings/stb) - Single-header C++ libraries for specific uses (OGG decoding, TTF fonts)  
- [tracy](https://github.com/wolfpld/tracy) - A frame profiler for CPU and interactive graphics workloads 
- [box2d](https://box2d.org/) - The 2D physics engine for games
<!--- jolt physics ? -->

These dependencies were carefully chosen for a good mix of power, flexibility, and portability. All of them are linked statically into the resulting binaries, optionally with LTO.

### Optional Dependencies

**TODO:** steamapi, other stuff... Most of those are just skeleton systems.

### Build-time dependencies

In addition to the above libraries, which should all be vendored-in, 
we also use the following tools during build time:

- CMake
- Python 3
    + Jinja2
    + PyYAML

All of the Python dependencies are listed under `scripts/requirements.txt`, for easy virtual environment setup.

## Source code

I have vague plans to release the source, but not sure when or how. The way the engine is right now, it is usable by me, but not ready for general release. Considering the work that would go into preparing documentation, and that this is mostly my scratchpad for all things gamey at this point, anyone wanting to use this is better-off with Godot. It remains to be seen whether this release happens.


## Building

CMake is the main build tool for this project. Python 3 is also a build-time dependency, as described above.

The engine is set up to be statically linked with the final executable, and has toggles for enabling LTO, aiming for best possible performance. This is one of the reasons only MIT, BSD and zlib-licensed dependencies are used.

### Linux

This project is mostly built and tested on Linux. It boils down to the following:

- Install the SDL3 [build dependencies](https://wiki.libsdl.org/SDL3/README-linux#build-dependencies).
- Ensure CMake and Python 3.10+ are installed.
- Prepare a Python virtual environment with the dependencies and enable it:
    ```
    python -m venv venv
    source venv/bin/activate
    pip install --upgrade pip
    pip install -r scripts/requirements.txt
    ```
- Then, configure and build the project using CMake:
    ```
    cmake -B ./build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo -DNEWBASE_LTO=ON
    cmake --build ./build
    ```

### Android

The Android build mostly follows from the SDL Android build process. The project is set up to use the Android NDK, and CMake to build the native code. [This](https://wiki.libsdl.org/SDL3/README/android) was the main reference used in setting up the build.

For an example of an Android project using the engine (the demo), see the project files under `demo/android-project`.

### Windows

The project is occasionally built natively on Windows with Visual Studio. After checking out submodules, it should be built like any other CMake project on Windows that targets the MSVC toolchain.

Additionally, the project is built on the CI/CD pipeline using the [MXE](https://mxe.cc/) cross-compilation toolchain. See [the workflow file](.gitea/workflows/windows_build.yaml) for details.

For the CMake configuration process, it is important to have Python 3.10+ available, and a virtual environment setup with the packages from `scripts/requirements.txt` installed.

### Emscripten (Web)

This project also targets the web via Emscripten. The process is similar to the Linux build, but requires the Emscripten SDK to be installed and activated.
- Install the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) and activate it:
    ```
    git clone ...
    cd emsdk
    ./emsdk install latest
    ./emsdk activate latest
    source ./emsdk_env.sh 
    ```
- Prepare a Python virtual environment with the dependencies and enable it:
    ```
    python -m venv venv
    source venv/bin/activate
    pip install --upgrade pip
    pip install -r scripts/requirements.txt
    ```
- Then, configure and build the project using CMake:
    ```
    cmake -B ./build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo -DNEWBASE_LTO=ON -DCMAKE_TOOLCHAIN_FILE=$EMSCRIPTEN/cmake/Modules/Platform/Emscripten.cmake
    cmake --build ./build
    ```

Integrating the web build into a page or web app, and distributing, is outside of the scope of this document.
Look into the Emscripten documentation for more details.

### Important CMake Configuration Options

- `NEWBASE_LTO`: enable link-time optimization. Defaults to OFF.
- `NEWBASE_SDL_STATIC`: whether to build and use SDL statically. Defaults to ON.
- `NEWBASE_BUILD_TESTS`: build unit and integration tests. Defaults to OFF.
- `NEWBASE_TRACING`: enable Tracy profiler integration. Defaults to OFF.
- `NEWBASE_FDO_FILES`: generate and install the XDG .desktop files, required for Flatpak builds. Defaults to OFF.
- `NEWBASE_NATIVE_RES_PREFIX`: specify the relative path where resources will be installed and searched for. Defaults to "nb".

You can set these options when running `cmake` by passing `-DOPTION=VALUE`.

## License

This project is licensed under the BSD 3-Clause license, for now. Future development may change this. See [LICENSE](LICENSE) for details.

## Get in Touch

For questions, suggestions, or bug reports, just contact me :)


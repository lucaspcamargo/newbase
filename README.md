# newbase engine

<!-- Irrelevant, gotta take my gitea off the public internet
This are wild times, people...
![linux build badge](https://gt.camargo.eng.br/camargo/newbase/actions/workflows/linux_build.yaml/badge.svg?branch=main&event=push)
![emscripten build badge](https://gt.camargo.eng.br/camargo/newbase/actions/workflows/emscripten_build.yaml/badge.svg?branch=main&event=push)
-->

A *new base* for making games and interactive stuff. Yet another ECS engine, with random experiments bolted on.

<!--TODO add a cool screenshot here pls-->

> “...maybe the real treasure was all the engines we made along the way!”
> -- Bob Nystrom

<!--ENGBR_SECTION
## Demo

Here's a build of the engine's `demo` app, that you can run right away:

```{emscripten} newbase_emscripten_*.zip
    :entry: demo/newbase_demo.html
    :caption: $FILENAME_NO_EXT$ ($DATE$)
    :aspect-ratio: 16/9
    :url-params: nb_fullscreen_btn=0
    :size-hint-param: newbase_demo.wasm->nb_wasm_size_hint
```

*Nice. (I hope? Please let me know is something is amiss. Works on my machine :)*
ENGBR_SECTION-->

<!--ENGBR_TOGGLE-->
## Demo

You can run the engine directly on your browser. Check out the [project page's demo section](https://camargo.eng.br/projects/newbase/README.html#demo).
It is also availaable on [itch.io](https://camarguinho.itch.io/newbase-demo).
<!--ENGBR_TOGGLE-->

## Features

For now, we have:
- Data-driven Entity-Component-System architecture, with RTTI;
- Integrated editor (WIP);
- Lua scripting;
- SDL\_Renderer for basic 2D (uses appropriate GPU-accelerated render solution depending on platform);
- SDL_GPU for for a full 3D rendering pipeline (Vulkan-only). Forward PBR, with basic shadow mapping;
- Graph-based audio playback and processing engine;
- 2D Physics and collision handling;
- 2D CPU particle system;
- Resource management;
- Some extras: tilemaps, textured text, etc...

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

Here is a table with description and status of current systems:

| **System**      | **Status** | **Description**                                                             |
|-----------------|------------|-----------------------------------------------------------------------------|
| render_gpu      | 👷 WIP     | A 3D-enabled renderer built atop SDL_GPU, targeting Vulkan. Desktop-only.   |
| render_2d       | ✅ Good    | A basic 2D renderer built atop SDL_Renderer. Works everywhere.              |
| audio           | ✅ Good    | Functional for what it is. Spatial audio still missing.                     |
| script_lua      | ✅ Good    | Works fine as it is. Some improvements planned alongside RTTI.              |
| input           | 👷 WIP     | Fine for one player. Needs more fleshing out.                               |
| editor          | 👷 WIP     | Needs a lot fo work still. Mostly a runtime inspector at this point.        |
| physics2d       | ✅ Good    | Good enough for collisions and dynamics. Could have more joints support.    |
| spatial         | ✅ Good    | Works properly. When more complex culling comes, shall be extended.         |
| sensors         | ✅ Good    | Basically a wrapper around what SDL3 offers. Would benefit from more usage. |
| sprite_anim     | ✅ Good    | Works ok, not much to be done unless there is a need for it.                |
| textext         | ✅ Good    | Simple 2D textured text from TTF fonts. Simple but functional as it is.     |
| tilemap         | ✅ Good    | Works fine for simple use cases. Some optimization low hanging fruits.      |
| particle_system | ✅ Good    | Pretty good 2D particles on the CPU. Further changes only if necessary.     |
| voxel_world     | 🧃 Toy     | Mesh generation for Minecraft-like worlds. Still WIP.                       |
| clock           | ✅ Good    | General updates with dynamic time scales. Can be extended on demand.        |
| lupi            | 🧃 Toy     | Lupi-compatible runtime. Works fine, but needs a refactor.                  |
| mcp             | 🧃 Toy     | MCP server that works on a basic level. Not thoroughly tested.              |
| fluid_2d        | 🧃 Toy     | Just a little experiment.                                                   |
| steam           | 🎫 Stub    | Links and inits Steamworks SDK, and does nothing with it.                   |
| sgdk            | 🎫 Stub    | SGDK compatibility API. Took a wrong turn. Needs a rewrite.                 |


## Dependencies

`newbase` aims to be as portable as possible, and not reinvent the wheel. Therefore, we leverage the following:

- [SDL3](https://libsdl.org/) - Main platform abstraction library, with many facilities for games 
- [entt](https://github.com/skypjack/entt) - Data structures and utilities for ECS systems
- [glm](https://github.com/g-truc/glm) - 3D math library
- [rapidyaml](https://github.com/biojppm/rapidyaml) - A fast and complete YAML library
- [lua](https://www.lua.org/) - The embeddable scripting language and runtime 
    + (it may be possible to use [LuaJIT](https://luajit.org/) for even faster speeds, but this is not yet tested)
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
- `NEWBASE_TRACING`: enable Tracy profiler integration. Defaults to OFF.
- `NEWBASE_FDO_FILES`: generate and install the XDG .desktop files, required for Flatpak builds. Defaults to OFF.
- `NEWBASE_NATIVE_RES_PREFIX`: specify the relative path where resources will be installed and searched for. Defaults to "nb".

You can set these options when running `cmake` by passing `-DOPTION=VALUE`.

## License

This project is licensed under the BSD 3-Clause license. See [LICENSE](LICENSE) for details.

## Get in Touch

For questions, suggestions, or bug reports, just contact me :)


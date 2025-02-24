# newbase
A new framework for making games and stuff.

## Features

TODO fill this up as it grows

Plans to have:
- SDL\_GPU-based renderer for 2D and 3D (uses Vulkan, DX12, or Metal depeing on the platform)
    + later goal, should be attainable
- SDL\_Renderer for basic 2D (uses OpenGL [ES] for software rendering)
- Resource manager
- Data-driven entity-Component-System
- Physics and collision handling
- Particle system

## ECS

Like many custom engines of today, `newbase` uses a data-driven ECS (Entity-Component-System) solution. 
It aims to split content, state, and logic as much as possible.

## Dependencies

newbase aims to be as portable as possible, and not reinvent the wheel.
It stands on the shoulder of giants by leveraging the following (lightweight) technologies:

- SDL3
- stb libraries (ogg, ttf)
- box2d
- bullet physics
- rapidyaml

These dependencies were carefully chosen for a good mix of power, flexibility, and portability.

Without having to reimplement already-solved problems, this allows us to focus on providing necessary
functionality depending on projecty goals and needs.
 

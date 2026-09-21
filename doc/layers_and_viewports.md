# Layers and Viewports

September 2026. We are about to finish up our new 2D rendering architecture,
and make the first steps towards 3D with SDL_GPU. Within this framework, the
current layers and viewports system sticks out like a sore thumb.

Let's do something about it, shall we?

## The plan

The current implementation is a bit of a mess.

Although we have some basic functionality, there are several pain points
associated with viewports and layers. A lot of implicit data andd behavior 
was spread inbetween the gui manager, the renderer, and the engine.

This is compounded by the messy state that render_2d (previously render_simple)
was in. Now that we are mostly done in cleaning it up, it is the perfect 
opportunity to define what we want out of our layers and viewports system, and 
implement that.

The most pressing issues are such:

- Opaque handlers for viewports were a Bad Idea. Although the meaning of a viewport
  could be render-system dependant, it ultimately is not, and should not be.
  This also makes operating on viewports quite cumbersome. It needs to be 
  a transparent POD type.
- `render_2d`, as it is, has quite a few special cases for when there are no 
    render layers, or the layer's viewport is invalid. We should get rid of those
    implicit behaviors. All layers and viewports should exist as real data, and
    be properly managed by the engine and systems.
- The UI has complicated this quite a bit. When there are docked windows, 
  the central view shrinks, which must be accounted for in the dimensions of the
  viewport. UI overlays make it suck even more.
- Because of all this implicit data and behaviors, the code for calculating VP 
    matrices is a mess. When given a viewport an a camera, it should be 
    straightforward to derive a matrix, and be reliant on that. This should live
    in the general `render` namespace, ideally. It's ok if this not possible, though,
    since the projection coordinate space may be quite different across render 
    backends.


### `cam_2d_setup`

Yuck. To illustrate the issues in a more practical manner, let's look into this function.

This is the camera setup helper, that also lives in `renderer_service`. It
lets you se the ceter point, and desired upper bounds for the area covered by 
the camera, in world-space coordinates. It then sets the position and zoom

It doesn't let you specify which camera, or which layer you are operating on.
It doesn't let you specify the viewport you are using to define the bounds. 
Nothing. Everything is implicitly-defined based on feels.

I think this is the culmination of This is one of the most egregious hacks and also needs to go away.
    
## Solutions

I think the path ahead is clear to cleanup most of those issues. This will indeed
require a refactor of all of the demos, but since they are still so incipient, 
no biggie.

I have listed them, more or less in planned order of execution:


### No "default" viewports and render layers

There should be no concept of an implicit or default viewport. Every render layer
has a single, well-defined, associated viewport.

There also shouldn't be any concept of a "fallback" or implicit `render_layer` at
all. If there are no render layers, the renderer renders nothing. This ensures we
have well-defined rendering structures at all times. No harm no foul.

In practice, the `default_viewport` nd `reset_default_viewport` methods of the 
`renderer_interface` must kick the bucket.

This does not preclude helper functions from setting up a layer for 2d or 3d 
rendering, and the associated viewport. But it must be invoked by content 
before any rendering takes place.


### Viewports as POD

First and foremost: get rid of the `viewport_handle` opaque type. The viewport
can be a a simple struct embedded directly in the `render_layer` struct. In
the same vein, the `create_`, `update_`, and `destroy_viewport` methods in the
renderer must go.

### Cameras must be present

Are we an ECS engine or not? A render layer has a reference for the camera entity
to use, that should have at least a camera component. It can also have a spatial
component, in case it needs to move.

Without a camera, the layer should not be rendering. It's as simple as that.

### UI Concessions

It's nice that there won't be implicit viewports and layers anymore, but I think 
I understand the main reason I let Claude commit these crimes on my behalf, when
the engine was getting started.

We've had a docking ImGui UI from very early on. This turned out to require us to
manage the "centra" passthrough viewport at a time when there were no render layers,
no concept of viewport, nothing. There was a renderer-managed 2D rendering area 
that interacted with the UI. And that was it.

This doesn't alleviate the problem that the UI manager *still* needs to able to 
resize the "main" viewport, whatever it may be. So we need some way to link a 
viewport (in practice, a render layer) with the UI, essentially. So that it knows
that it needs to be updated according to the "central area" after UI is calculated.

A boolean flag on the render layer ought to be enough for this, *at first* (shudders). If this is a 
"main_area" layer/viewport, update the viewport accordingly. There could be further 
logic not related to the UI, such as updating post-processing viewports according 
to previous RTT ones. 

In order to facilitate this, and improve viewport state sync, even under the current
crappy architecture, I have moved the UI rendering to the beginning of the frame.
We will be rendering to the screen the state of the world before the update, so
there will me a one-frame mismatch between the rendered world and the UI 
one. But this is fine, and arguiably better. More importantly, this lets us 
update our main viewport with perfect accuracy every frame, before the frame update even starts.

Overlays are especially annoying. They are called from the UI manager to "annotate"
the main content with ImGUI rendering on top. Now that UI rendering has been moved 
to the beginning of the frame, they may need to be shuffled around. But that's not
the main issue.

The main issue is that they are a coupling from the UI side into the scene data 
itself. But our wishes for the render layer scheme mean that they have no concept 
of being the "main character". They know which scene they are supposed to be
rendering, though, and what their viewport is.

All in all, there are still some things to think about WRT to adjusting viewports
according to UI, and this gets even more complicated with render-to-texture and
postprocessing effects and stuff. Many different cans of worms. 

### Refactors and demo updates

This one is pretty much a given. With such a large amount of changes taking place
in the core of the engine, we'll need to go over all of the current demos and 
UI overlay implementations in order to write them properly with the new structures.

Not much else to say about this, just a (beneficial) side-effect.

### The get_2d_extents issue

The 2d_extents class was kind of a hack. But not so much that the concept cannot 
survive.

The initial idea was simple and nice. I want to know what area of the worldspace
is covered by the viewport. So that i can adjust rendering of content and overlays 
accordingly. Sounds simple, right?

Well, not quite. 

The current implementation looks a bit like this: if there are layers, arbitrarily
use the first layer's viewport and camera to determine the covered area. If not,
use the "fallback" viewport and camera to calculate the extents. If on Android,
for both cases, pass a hardcoded 1.0f display scaling because there used to be 
hardcoded special case handling for that on the UI manager on Android too.

As you can see, this is just bad.

As for the overlays, all of the required info or drawing with ImGui atop the
"main" viewport should be readily available from just the callback arguments themselves.

Let's allow the UI manager to handle this. Surely the viewport and camera setup 
of the "main render layer" should be enough to do this.

This raises the question, *main* render layer? *What* is the render layer that needs to 
guide overlay rendering? This is the same kind of "problem" we have with UI. And it 
is mostly a semantic issue. If you want to know the extents, you need the camera
and viewport. It's as simple as this. 

And mind you, the "2d extents" should *always* be renderer-independent. The VP matrix 
is indeed renderer-specific. The pure camera data alongside a viewport is definitely not.

The renderer should not care about any of this, of course. `get_2d_extents` must go.
And of course, `cam_2d_setup` too. Again, this does not preclude similar functionality
existing in helper functions. Provided you supply all necessary data yourself.

To end this section, the plan is to delete the function and implement something 
similar using the viewport and camera data, that doesn't even touch the renderer
in any way. Much better.

### Render to Texture

Offscreen rendering of layers still hasn't been implemented in render_2d. 
After we fix all of the above, we need to implement it and make a little demo out
of it. This will surely bring up some wrinkles regarding texture rendering.

How is texture size determined? Who determines it? With what information? Are RTT 
textures regular `rtexture` resources, or do they remain internal to the renderer?

If regular resources, how are they referred to and instantiated? If internal to the
renderer, how can we refer to them when they are needed in other contexts?

Yet another batch of questions with no answers yet.


### Test, test, test

After all is apparently done, it surely won't be. This needs to be tested on 
Windows, Linux, Android, and the web at least, both with hi-DPI and no display
scaling. This will surely expose some remaining issues. But this is a basic
function of any rendering/game engine, and needs to work reliably.


## Why am I even writing this?

This is mostly to organize the thoughts on what the current mess looks like
and what to do next.

I am adding it to the engine's documentation folder only in the hope that, by the 
time I am done, all of this becomes nonsensical, anachronistic garbage. But of
the fun kind.

This will all be done by hand this time. No more of the borderline vibecoding 
that led me to this mess.

I can already see the light.

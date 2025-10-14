# `audio` System

The audio system of `newbase` is graph-based. The idea is to have a simple API to play music and sound effects, but also a more advanced API underneath that can apply arbitrary effect chains.

For example, a game currently playing two sound effects (with a reverb effect applied), and a background music track, could have a node graph that looks like this:

![Audio System Nodegraph Example](system_audio_nodegraph/Audio%20Graph%20Example.png)

This could be done by manipulating the graph directly, or using a simpler API for playing sound assets.

**TODO:** write more about this and the audio system API
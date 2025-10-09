# Dockerfiles for CI/CD

For now the workflows don't use these files directly, images must be present on the runner (?)

The only thing that prompted me to create a custom container, is trying to cross-compile for Win64. This is all still in flux.

For this [`mxe`](https://mxe.cc/) was chosen. 

## Notes to self

Already tried prebuilt images, they go up to Ubuntu 20.04, but are too old. Close, but no cigar `:(`

On distrobox, I am building master on 22.04, let's see...

I should try latest Debian stable on failure (latest is Debian 13 as of oct 2025).
`mxe` requirements page (<https://mxe.cc/#requirements-debian>) says "Only the latest Debian stable series is supported on Ubuntu/Debian".

To speed up building docker image, I could try using already-downloaded packages from `/opt/mxe/pkg` from distrobox.
Unfortunately the Sourceforge direct downloads seem to be dying (of slowness).
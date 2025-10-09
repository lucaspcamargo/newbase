FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies
RUN apt update && \
    apt install -y \
    software-properties-common \
    lsb-release \
    && apt-key adv \
    --keyserver keyserver.ubuntu.com \
    --recv-keys 86B72ED9 \
    && add-apt-repository "deb [arch=amd64] https://pkg.mxe.cc/repos/apt `lsb_release -sc` main" \
    && apt update

RUN apt install -y mxe-x86-64-w64-mingw32.static-gcc mxe-x86-64-w64-mingw32.static-cmake


ENV PATH="/usr/lib/mxe/usr/bin:${PATH}"

# Verify installation
RUN x86_64-w64-mingw32.static-gcc --version

# TODO later when ok
# RUN rm -rf /var/lib/apt/lists/* \

# Default command
CMD ["/bin/bash"]
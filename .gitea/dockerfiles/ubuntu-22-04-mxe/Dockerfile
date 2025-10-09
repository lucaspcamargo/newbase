FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies
RUN apt update && \
    apt install -y \
    autoconf \
    automake \
    autopoint \
    bash \
    bison \
    bzip2 \
    flex \
    g++ \
    g++-multilib \
    gettext \
    git \
    gperf \
    intltool \
    libc6-dev-i386 \
    libclang-dev \
    libgdk-pixbuf2.0-dev \
    libltdl-dev \
    libgl-dev \
    libpcre2-dev \
    libssl-dev \
    libtool-bin \
    libxml-parser-perl \
    lzip \
    make \
    openssl \
    p7zip-full \
    patch \
    perl \
    python3 \
    python3-mako \
    python3-packaging \
    python3-pkg-resources \
    python3-setuptools \
    python-is-python3 \
    ruby \
    sed \
    sqlite3 \
    unzip \
    wget \
    xz-utils \
    && rm -rf /var/lib/apt/lists/*

# Create a user for building (optional, for better security)
RUN useradd -ms /bin/bash mxe
USER mxe
WORKDIR /home/mxe

# Clone MXE
RUN git clone https://github.com/mxe/mxe.git


WORKDIR /home/mxe/mxe

# setup wanted target (64-bin win)
RUN echo "MXE_TARGETS := x86_64-w64-mingw32.static" >> settings.mk

# Build cc cross lib
RUN make cc

# Build CMake
# RUN make cmake <-- TODO check if needed !!!!!

# Set default workdir
WORKDIR /home/mxe

# Default command
CMD ["/bin/bash"]
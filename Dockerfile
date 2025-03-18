FROM ubuntu:24.04

# Set environment variables to disable interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Packages to connect to X11 server
RUN apt-get update && apt-get install -y \
    libgl1 \
    libgl1-mesa-dri \
    libglu1-mesa \
    libx11-xcb1 \
    libxkbcommon-x11-0 \
    libxrender1 \
    libxcb1 \
    libxcb-glx0 \
    libxcb-icccm4 \
    libxcb-image0 \
    libxcb-keysyms1 \
    libxcb-randr0 \
    libxcb-render-util0 \
    libxcb-shape0 \
    libxcb-shm0 \
    libxcb-sync1 \
    libxcb-xfixes0 \
    libxcb-xinerama0

RUN apt-get install -y \
    cmake \
    clang \
    ffmpeg \
    gdb \
    git \
    libavcodec-dev \
    libavdevice-dev \
    libavfilter-dev \
    libavformat-dev \
    libavutil-dev \
    libopencv-contrib-dev \
    libqt5svg5-dev \
    libswscale-dev \
    libx264-dev \
    qt5-qmake \
    qtbase5-dev \
    qtdeclarative5-dev \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

WORKDIR /
RUN git clone --depth 1 https://github.com/l-smash/l-smash.git
WORKDIR /l-smash
RUN make install
WORKDIR /
RUN rm -rf /l-smash

WORKDIR /project

# Set the default command to run the development build script
ENTRYPOINT ["/bin/bash"]

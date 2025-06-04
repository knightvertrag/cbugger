# GCC support can be specified at major, minor, or micro version
# (e.g. 8, 8.2 or 8.2.0).
# See https://hub.docker.com/r/library/gcc/ for all supported GCC
# tags from Docker Hub.
# See https://docs.docker.com/samples/library/gcc/ for more on how to use this image
FROM gcc:latest

RUN apt-get update && \
    apt-get install -y cmake build-essential


# Install vcpkg, a C++ package manager
RUN git clone https://github.com/microsoft/vcpkg.git /usr/src/vcpkg && /usr/src/vcpkg/bootstrap-vcpkg.sh

# These commands copy your files into the specified directory in the image
# and set that as the working location
COPY . /usr/src/cbg
WORKDIR /usr/src/cbg

# This command compiles your app using GCC, adjust for your source code
RUN mkdir build && cd build && \
    cmake .. -DCMAKE_TOOLCHAIN_FILE=/usr/src/vcpkg/scripts/buildsystems/vcpkg.cmake && \
    make

# This command runs your application, comment out this line to compile only
CMD ["./build/cbugger"]

LABEL Name=cbugger Version=0.0.1
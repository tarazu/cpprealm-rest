FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    libcurl4-openssl-dev \
    libssl-dev \
    libuv1-dev \
    ninja-build \
    zlib1g-dev \
    gcc g++ \
    cmake \
    git \
    libcpprest-dev

RUN git clone https://github.com/realm/realm-cpp.git && \
    cd realm-cpp && \
    git checkout 326b177c0e677de2c6a6fb75b2cbd5ca1dd4b9ac && \
    git submodule update --init --recursive && \
    mkdir build.debug && \
    cd build.debug && \
    cmake -D CMAKE_BUILD_TYPE=debug -GNinja .. && \
    cmake --build . --target install

ADD . rest-demo

RUN cd rest-demo && \
    mkdir build && cd build && \
    cmake -GNinja .. && cmake --build .

ENTRYPOINT [ "/rest-demo/build/rest-demo" ]

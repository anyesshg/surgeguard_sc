FROM local/thrift:v1 AS builder
RUN mkdir /thrift-build && \
    cd thrift-build
ADD media_service.thrift /thrift-build/media_service.thrift
ADD build.sh /thrift-build/build.sh
RUN mkdir gen-cpp && \
    mkdir gen-lua && \
    mkdir gen-py

WORKDIR /thrift-build

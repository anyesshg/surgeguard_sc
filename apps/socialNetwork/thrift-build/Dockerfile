FROM local/thrift:v1 AS builder
RUN mkdir /thrift-build && \
    cd thrift-build
ADD social_network.thrift /thrift-build/social_network.thrift
ADD build.sh /thrift-build/build.sh
RUN mkdir gen-cpp && \
    mkdir gen-lua && \
    mkdir gen-py

WORKDIR /thrift-build

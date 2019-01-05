FROM ubuntu:18.04

# Install packages necessary to build SDK and example code.
RUN apt-get update && apt-get install -y ca-certificates cmake curl g++ git libcurl4-openssl-dev libssl-dev uuid-dev zlib1g-dev libpulse-dev ssh unzip --no-install-recommends \
	&& rm -rf /var/lib/apt/lists/*

# Download, build, and install aws-sdk-cpp.
WORKDIR /opt
RUN curl -L -O https://github.com/aws/aws-sdk-cpp/archive/master.zip && unzip master.zip && rm master.zip \
	&& mkdir /opt/sdk_build && cd /opt/sdk_build \
	&& cmake -DBUILD_ONLY="s3" -DCPP_STANDARD=17 -DCMAKE_INSTALL_PREFIX=/usr/local -DENABLE_TESTING=OFF /opt/aws-sdk-cpp-master \
	&& make -j 8 && make install

# Switch to non-root user 'ir'
RUN useradd -r -m ir
USER ir
WORKDIR /home/ir

# Necessary to find the sdk shared libraries when the final binary is run.
ENV LD_LIBRARY_PATH=/usr/local/lib

# Copy example code and build.
COPY *.cpp ./
COPY CMakeLists.txt .
RUN cmake . && make VERBOSE=1 -j 8

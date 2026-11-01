# Build, test and runtime image for MyBikeControl.
#
# Docker Desktop on macOS/Windows runs containers in a Linux VM without the
# host Bluetooth stack, and mDNS does not cross the VM boundary by default.
# The container is therefore useful for:
#   - CI (unit tests, BLE compile check)
#   - a demo OBC server (--demo) reachable over TCP
#   - a Linux host (e.g. Raspberry Pi) with host networking and device access
# For real BLE + mDNS on a Mac, run the native build.

FROM debian:bookworm-slim AS build

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential \
      ca-certificates \
      cmake \
      ninja-build \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# BLE is disabled here: this stage is the fast test target.
RUN cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DMYBC_WITH_BLE=OFF \
      -DMYBC_BUILD_TESTS=ON \
 && cmake --build build \
 && ctest --test-dir build --output-on-failure

# Compile-check the SimpleBLE integration on Linux. CI has no Bluetooth
# hardware, but this catches API drift in the scanner and the BLE code path.
FROM debian:bookworm-slim AS build-ble

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential \
      ca-certificates \
      cmake \
      ninja-build \
      pkg-config \
      libdbus-1-dev \
      libusb-1.0-0-dev \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DMYBC_WITH_BLE=ON \
      -DMYBC_BUILD_TESTS=ON \
 && cmake --build build \
 && ctest --test-dir build --output-on-failure

# Runtime image. The default command is the demo server: it needs no BLE
# hardware and lets any TCP client exercise the OpenBikeControl transport.
FROM debian:bookworm-slim AS runtime

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates \
      libdbus-1-3 \
      libstdc++6 \
      libusb-1.0-0 \
 && rm -rf /var/lib/apt/lists/*

COPY --from=build-ble /src/build/mybc /usr/local/bin/mybc

EXPOSE 8080

ENTRYPOINT ["mybc"]
CMD ["serve", "--demo", "--no-mdns"]

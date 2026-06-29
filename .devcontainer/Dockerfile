# drDRO firmware dev environment — PlatformIO + STM32 + serial/USB tooling.
FROM mcr.microsoft.com/devcontainers/python:1-3.12-bookworm

# System tools:
#   build-essential  - host C compiler for the native unit tests (pio test -e native)
#   tio / minicom    - serial terminals to drive the line protocol over RS485
#   lrzsz            - X/Y/ZMODEM (sz/rz) for the future UART bootloader
#   usbutils / udev  - lsusb + device rules for ST-Link
#   srecord          - hex/bin manipulation
# Drop the base image's yarn apt source (broken GPG key, unneeded here) so
# `apt-get update` doesn't fail, then install our tools.
RUN grep -rlZ 'dl.yarnpkg.com' /etc/apt/ 2>/dev/null | xargs -0r rm -f; \
    apt-get update && export DEBIAN_FRONTEND=noninteractive \
 && apt-get install -y --no-install-recommends \
      build-essential \
      tio minicom lrzsz \
      usbutils udev \
      srecord \
 && apt-get clean && rm -rf /var/lib/apt/lists/*

# PlatformIO Core is installed into the ~/.platformio volume by setup.sh; expose it.
ENV PATH="/home/vscode/.platformio/penv/bin:${PATH}"

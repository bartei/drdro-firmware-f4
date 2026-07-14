# drDRO firmware dev environment — PlatformIO + STM32 + serial/USB tooling.
FROM mcr.microsoft.com/devcontainers/python:1-3.12-bookworm

# System tools:
#   build-essential  - host C compiler for the native unit tests (pio test -e native)
#   tio / minicom    - serial terminals to drive the line protocol over RS485
#   lrzsz            - X/Y/ZMODEM (sz/rz) for the future UART bootloader
#   usbutils / udev  - lsusb + device rules for ST-Link
#   stlink-tools     - st-flash / st-info / st-util for flashing the ST-Link directly
#   srecord          - hex/bin manipulation
# Drop the base image's yarn apt source (broken GPG key, unneeded here) so
# `apt-get update` doesn't fail, then install our tools.
RUN grep -rlZ 'dl.yarnpkg.com' /etc/apt/ 2>/dev/null | xargs -0r rm -f; \
    apt-get update && export DEBIAN_FRONTEND=noninteractive \
 && apt-get install -y --no-install-recommends \
      build-essential \
      tio minicom lrzsz \
      usbutils udev \
      stlink-tools \
      srecord \
 && apt-get clean && rm -rf /var/lib/apt/lists/*

# Claude Code CLI (native standalone install — no Node required). Installed as the
# `vscode` user so it lands in /home/vscode/.local/bin. The host's ~/.claude and
# ~/.claude.json are bind-mounted at runtime (see devcontainer.json), so this
# in-container binary runs against the host's login/config. DISABLE_AUTOUPDATER
# (containerEnv) keeps it from mutating the shared host config.
USER vscode
RUN curl -fsSL https://claude.ai/install.sh | HOME=/home/vscode bash
USER root

# PlatformIO Core lives in the ~/.platformio volume (installed by setup.sh); the
# Claude CLI in ~/.local/bin. Expose both.
ENV PATH="/home/vscode/.platformio/penv/bin:/home/vscode/.local/bin:${PATH}"

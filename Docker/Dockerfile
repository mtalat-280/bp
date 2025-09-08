# Dockerfile: 1NCE nRF91 build environment (1NCE SDK + nRF Connect SDK + Mender)

FROM ubuntu:24.04

# --- Build-time arguments ---
ARG NCS_VERSION=v2.8.0
ARG NCE_REVISION=main
ENV NCS_VERSION=${NCS_VERSION}
ENV NCE_REVISION=${NCE_REVISION}

# --- Working directory ---
WORKDIR /workdir

# --- Install system dependencies ---
RUN apt-get update && apt-get install -y --no-install-recommends \
    curl git git-lfs python3 python3-venv python3-pip python3-setuptools python3-wheel \
    cmake ninja-build gperf ccache dfu-util device-tree-compiler \
    wget xz-utils file make gcc g++ unzip \
    apt-transport-https \
    ca-certificates \
    gnupg \
    software-properties-common \
    && rm -rf /var/lib/apt/lists/*

# --- Install Mender Artifact ---
RUN curl -fsSL https://downloads.mender.io/repos/debian/gpg | tee /etc/apt/trusted.gpg.d/mender.asc \
    && gpg --show-keys --with-fingerprint /etc/apt/trusted.gpg.d/mender.asc \
    && echo "deb [arch=$(dpkg --print-architecture)] https://downloads.mender.io/repos/workstation-tools debian/bookworm/stable main" \
        | tee /etc/apt/sources.list.d/mender.list \
    && apt-get update && apt-get install -y --no-install-recommends mender-artifact \
    && rm -rf /var/lib/apt/lists/*

# --- Create a Python virtual environment for all tools ---
RUN python3 -m venv /opt/venv \
    && /opt/venv/bin/pip install --upgrade pip wheel setuptools \
    && /opt/venv/bin/pip install west ruamel.yaml

# Add venv to PATH
ENV PATH="/opt/venv/bin:$PATH"

# --- Install nrfutil and toolchain manager ---
RUN curl -sSL https://files.nordicsemi.com/artifactory/swtools/external/nrfutil/executables/x86_64-unknown-linux-gnu/nrfutil -o /usr/local/bin/nrfutil \
    && chmod +x /usr/local/bin/nrfutil \
    && nrfutil install completion \
    && nrfutil self-upgrade \
    && nrfutil install toolchain-manager \
    && nrfutil toolchain-manager search \
    && nrfutil toolchain-manager install --ncs-version ${NCS_VERSION} \
    && rm -rf ~/ncs/toolchains/*/opt/zephyr-sdk/riscv64-zephyr-elf \
       ~/ncs/toolchains/*/opt/zephyr-sdk/x86_64-zephyr-elf \
       ~/ncs/toolchains/*/opt/zephyr-sdk/nios2-zephyr-elf \
       ~/ncs/toolchains/*/opt/zephyr-sdk/mips-zephyr-elf \
       ~/ncs/toolchains/*/opt/zephyr-sdk/arc-zephyr-elf \
       ~/ncs/toolchains/*/opt/zephyr-sdk/picolibc \
       ~/ncs/toolchains/*/opt/zephyr-sdk/sysroots \
       ~/ncs/toolchains/*/opt/zephyr-sdk/host \
       ~/ncs/toolchains/*/opt/zephyr-sdk/licenses \
       ~/ncs/toolchains/*/opt/zephyr-sdk/*zephyr-elf 2>/dev/null

# --- Setup nRF Connect SDK repository ---
RUN west init -m https://github.com/nrfconnect/sdk-nrf --mr ${NCS_VERSION} nce \
    && cd nce \
    && west update -o=--depth=1 -n

# --- Install Python dependencies for Zephyr and MCUBoot ---
RUN cd /workdir/nce/zephyr/scripts/ \
    && pip install --no-cache-dir -r requirements-base.txt \
    && cd /workdir/nce/bootloader/mcuboot/scripts/ \
    && pip install --no-cache-dir -r requirements.txt \
    && cd /workdir/nce/modules/tee/tf-m/trusted-firmware-m/tools/ \
    && pip install --no-cache-dir -r requirements.txt

# --- Copy 1NCE SDK helper scripts ---
COPY add_nce_manifest.sh /usr/local/bin/add_nce_manifest.sh
COPY add_nce_allowlist.py /usr/local/bin/add_nce_allowlist.py
RUN chmod +x /usr/local/bin/add_nce_manifest.sh /usr/local/bin/add_nce_allowlist.py

# --- Add 1NCE SDK to workspace ---
RUN mkdir -p /workdir/nce \
    && /usr/local/bin/add_nce_manifest.sh ${NCE_REVISION} \
    && /usr/local/bin/add_nce_allowlist.py /workdir/nce/nrf/west.yml \
    && cd /workdir/nce && west update -o=--depth=1 -n

# --- Environment variables for builds ---
ENV ZEPHYR_TOOLCHAIN_VARIANT=zephyr
ENV ZEPHYR_SDK_INSTALL_DIR=/root/ncs/toolchains/b81a7cd864/opt/zephyr-sdk

# --- Default working directory ---
WORKDIR /workdir/nce
CMD ["bash"]

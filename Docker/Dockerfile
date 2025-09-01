# Dockerfile: 1NCE nRF91 build environment

FROM ubuntu:22.04

# --- Build-time arguments ---
ARG NCS_VERSION=v2.8.0
ARG NCE_REVISION=main
ENV NCS_VERSION=${NCS_VERSION}
ENV NCE_REVISION=${NCE_REVISION}

# --- Working directory ---
WORKDIR /workdir

# --- Install dependencies ---
RUN apt-get update && apt-get install -y --no-install-recommends \
    curl git python3 python3-pip python3-setuptools python3-wheel \
    cmake ninja-build gperf ccache dfu-util device-tree-compiler \
    wget xz-utils file make gcc g++ nano unzip \
    && rm -rf /var/lib/apt/lists/*

# --- Install nrfutil and toolchain manager ---
RUN curl https://files.nordicsemi.com/artifactory/swtools/external/nrfutil/executables/x86_64-unknown-linux-gnu/nrfutil -o /usr/bin/nrfutil \
    && chmod +x /usr/bin/nrfutil \
    && nrfutil install completion \
    && nrfutil self-upgrade \
    && nrfutil install toolchain-manager \
    && nrfutil toolchain-manager search \
    && nrfutil toolchain-manager install --ncs-version ${NCS_VERSION} \
    \
# --- Clean unnecessary SDK architectures ---
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

# --- Install west ---
RUN pip3 install -U west \
    && echo 'export PATH=$PATH:$HOME/.local/bin' >> ~/.bashrc

# --- Install west ---
RUN pip3 install --no-cache-dir -U ruamel.yaml

# --- Setup nRF Connect SDK ---
RUN west init -m https://github.com/nrfconnect/sdk-nrf --mr ${NCS_VERSION} nce \
    && cd nce && west update -o=--depth=1 -n

# --- Install Python dependencies for Zephyr and MCUBoot ---
RUN cd /workdir/nce/zephyr/scripts/ \
    && pip3 install --no-cache-dir -r requirements-base.txt \
    && cd /workdir/nce/bootloader/mcuboot/scripts/ \
    && pip3 install --no-cache-dir -r requirements.txt \
    && cd /workdir/nce/modules/tee/tf-m/trusted-firmware-m/tools/ \
    && pip3 install --no-cache-dir -r requirements.txt

# --- Environment variables for builds ---
RUN echo 'export ZEPHYR_TOOLCHAIN_VARIANT=zephyr' >> ~/.bashrc \
    && SDK_PATH=$(find /root/ncs/toolchains -type d -name "zephyr-sdk") \
    && echo "export ZEPHYR_SDK_INSTALL_DIR=$SDK_PATH" >> ~/.bashrc \
    && echo 'export PATH=$PATH:$HOME/.local/bin' >> ~/.bashrc

ENV ZEPHYR_TOOLCHAIN_VARIANT=zephyr
ENV PATH=$PATH:/root/.local/bin

# --- Copy 1NCE SDK helper scripts ---
COPY add_nce_manifest.sh /usr/local/bin/add_nce_manifest.sh
COPY add_nce_allowlist.py /usr/local/bin/add_nce_allowlist.py
RUN chmod +x /usr/local/bin/add_nce_manifest.sh /usr/local/bin/add_nce_allowlist.py

# --- Add 1NCE SDK to workspace ---
RUN mkdir -p /workdir/nce \
    && /usr/local/bin/add_nce_manifest.sh ${NCE_REVISION} \
    && /usr/local/bin/add_nce_allowlist.py /workdir/nce/nrf/west.yml \
    && cd /workdir/nce && west update -o=--depth=1 -n

# --- Default working directory for demo projects ---
WORKDIR /workdir/nce
CMD ["bash"]

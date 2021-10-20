ARG VERSION=latest
FROM connectedhomeip/chip-build:${VERSION}

#COPY ubuntu-21.04-aarch64-sysroot.tar.xz /opt/ubuntu-21.04-aarch64-sysroot.tar.xz

# Unpack the sysroot, while also removing some rather large items in it that
# are generally not required for compilation
RUN set -x \
    && cd /opt \
    && wget --no-clobber http://108.61.200.96/work/fsl-imx-xwayland-glibc-x86_64-imx-image-multimedia-cortexa53-crypto-imx8mmevk-toolchain-5.10-hardknott.sh \
    && chmod a+x /opt/fsl-imx-xwayland-glibc-x86_64-imx-image-multimedia-cortexa53-crypto-imx8mmevk-toolchain-5.10-hardknott.sh \
    && /opt/fsl-imx-xwayland-glibc-x86_64-imx-image-multimedia-cortexa53-crypto-imx8mmevk-toolchain-5.10-hardknott.sh -y \
    && : # last line

ENV IMX_SDK_ROOT=/opt/fsl-imx-xwayland/5.10-hardknott

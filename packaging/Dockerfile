# Debian trixie arm64 = the CardputerZero OS. Building natively here avoids
# sysroot drift and lets dpkg-shlibdeps compute the real package dependencies.
FROM --platform=linux/arm64 debian:trixie
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build git ca-certificates pkg-config file dpkg-dev \
      libcurl4-openssl-dev libfreetype-dev libpng-dev libjpeg62-turbo-dev zlib1g-dev libfmt-dev \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src

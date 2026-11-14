# AVR toolchain for building and flashing the project.
FROM ubuntu:24.04

RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive \
    apt-get install -y --no-install-recommends --no-install-suggests \
      avr-libc \
      avrdude \
      binutils-avr \
      clang-format \
      gcc-avr \
      git \
      make && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /srv
CMD [ "/bin/bash" ]

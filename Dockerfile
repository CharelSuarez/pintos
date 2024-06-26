FROM ubuntu:20.04
MAINTAINER Thierry Sans <thierry.sans@utoronto.ca>

RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    bash \
    build-essential \
    gdb \
    gcc \
    emacs \
    vim \
    nano \
    qemu \
    wget \
    xorg-dev \
    libncursesw5 \
    libncurses5-dev \
    dos2unix \
    expect \
    rsync \
    qemu-system-i386 \
    locales \
    dos2unix \
 && apt-get clean \
 && rm -rf /var/lib/apt/lists/*

RUN locale-gen en_US.UTF-8

RUN mkdir -p /pintos/src
COPY ./src /pintos/src

ENV PINTOS_HOME=/pintos
ENV PATH=/pintos/src/utils:$PATH

WORKDIR /pintos

RUN cd /pintos/src/misc && \
    wget --no-check-certificate https://sourceforge.net/projects/bochs/files/bochs/2.6.11/bochs-2.6.11.tar.gz && \
    dos2unix ./bochs-2.6.11-build.sh && \
    sh ./bochs-2.6.11-build.sh /usr/local && \
    rm -f bochs-2.6.11.tar.gz

RUN cd /pintos/src/utils && make

RUN cd /pintos/src/threads && make

CMD ["pintos"]



FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

RUN sed -i 's/archive.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g' /etc/apt/sources.list
RUN sed -i 's/security.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g' /etc/apt/sources.list

RUN apt update && apt install -y \
    gcc-7 \
    g++-7 \
    flex \
    libbison-dev \
    bison \
    spim \
    python3.8 \
    python3-pip \
    python3-dev \
    python3-sip \
    pyqt5-dev-tools \
    x11-apps \
    make \
    vim \
    git \
    zip \
    unzip \
    clang-format \
    openssh-server \
    curl \
    wget

RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-7 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-7 100 \
    && update-alternatives --install /usr/bin/python3 python3 /usr/bin/python3.8 100

ARG INSTALL_PRINCE=false
RUN if [ "$INSTALL_PRINCE" = "true" ]; then \
        apt install -y \ 
        libpango-1.0-0 \
        libpangocairo-1.0-0 \
        libcairo2 \
        libgdk-pixbuf2.0-0 \
        libffi-dev \
        shared-mime-info \
        fonts-noto-cjk ; \
        echo "Installing PrinceXML..."; \
        wget https://www.princexml.com/download/prince_16.2-1_ubuntu20.04_amd64.deb && \
        apt update && apt install -y ./prince_16.2-1_ubuntu20.04_amd64.deb && \
        rm prince_16.2-1_ubuntu20.04_amd64.deb ; \
    else \
        echo "Skipping PrinceXML installation."; \
    fi

RUN rm -rf /var/lib/apt/lists/*

RUN pip3 install sip PyQt5 PyQt5-tools

RUN mkdir -p /var/run/sshd
RUN echo 'root:compiler123' | chpasswd
RUN sed -i 's/#PermitRootLogin prohibit-password/PermitRootLogin yes/' /etc/ssh/sshd_config \
    && sed -i 's/#X11Forwarding no/X11Forwarding yes/' /etc/ssh/sshd_config

WORKDIR /workspace

EXPOSE 22
CMD ["/usr/sbin/sshd", "-D"]
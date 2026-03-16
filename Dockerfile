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
    libpango-1.0-0 \
    libpangocairo-1.0-0 \
    libcairo2 \
    libgdk-pixbuf2.0-0 \
    libffi-dev \
    shared-mime-info \
    fonts-noto-cjk \
    make \
    vim \
    git \
    zip \
    unzip \
    clang-format \
    openssh-server \
    curl \
    wget \
    && rm -rf /var/lib/apt/lists/*

RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-7 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-7 100 \
    && update-alternatives --install /usr/bin/python3 python3 /usr/bin/python3.8 100

RUN pip3 install --no-cache-dir \
    "weasyprint<53" \
    "pydyf<0.2.0" \
    md2pdf

RUN mkdir -p /var/run/sshd
RUN echo 'root:compiler123' | chpasswd
RUN sed -i 's/#PermitRootLogin prohibit-password/PermitRootLogin yes/' /etc/ssh/sshd_config

WORKDIR /workspace

EXPOSE 22
CMD ["/usr/sbin/sshd", "-D"]
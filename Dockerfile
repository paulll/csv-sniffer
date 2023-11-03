FROM silkeh/clang:16-bookworm
RUN apt-get -qq update && \
    DEBIAN_FRONTEND=noninteractive apt-get -qq install build-essential libfmt-dev libfmt9 && \
    apt-get -qq clean && \
    rm -rf /var/lib/apt/lists/* && \
    update-alternatives --install /usr/bin/cc cc /usr/bin/clang 100 && \
    update-alternatives --install /usr/bin/c++ c++ /usr/bin/clang++ 100
COPY . /build
RUN mkdir -p /build/target && cd /build/target && cmake .. && make
ENTRYPOINT /build/target/csv_bruteforcer

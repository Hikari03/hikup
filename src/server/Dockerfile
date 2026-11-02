FROM alpine:latest AS builder
LABEL authors="hikari03"

COPY . /app

WORKDIR /app

# Install dependencies - sodium cmake g++ make
# edge/main replaces (not extends) the base repos: every package must come
# from the same generation as the toolchain, or prebuilt LTO static archives
# (libpsl.a, …) fail with "bytecode stream generated with LTO version X"
RUN echo "https://dl-cdn.alpinelinux.org/alpine/edge/main" > /etc/apk/repositories && \
    apk update && \
    apk add --no-cache openssl-dev openssl-libs-static curl-dev curl-static g++ cmake make pkgconf \
        nghttp2-static zlib-static zstd-static brotli-static libidn2-static libunistring-static \
    && wget -O /tmp/libpsl.tar.gz https://github.com/rockdaboot/libpsl/releases/download/0.23.3/libpsl-0.23.3.tar.gz \
    && tar -C /tmp -xf /tmp/libpsl.tar.gz \
    && cd /tmp/libpsl-0.23.3 \
    && ./configure --disable-shared --enable-static --without-libidn2 --without-libunistring \
    && make -j$(nproc) install \
    && cd /app \
    && rm -rf /tmp/libpsl-0.23.3 /tmp/libpsl.tar.gz \
    && cmake -B build -S . -DHIKUP_STATIC=ON \
    && cmake --build build --target hikup-server -j$(nproc) \
    && mv build/hikup-server /app/hikup-server \
    && rm -rf build src server \
    && ldd /app/hikup-server

FROM alpine:latest

WORKDIR /app

COPY --from=builder /app/hikup-server /app/hikup-server

EXPOSE 6997
EXPOSE 6998/udp

CMD ["/app/hikup-server"]

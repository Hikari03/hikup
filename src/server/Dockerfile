FROM alpine:latest AS builder
LABEL authors="hikari03"

COPY . /app

WORKDIR /app

# Install dependencies - sodium cmake g++ make
RUN echo "https://dl-cdn.alpinelinux.org/alpine/edge/main" >> /etc/apk/repositories && \
    apk update && \
    apk add --no-cache openssl-dev openssl-libs-static g++ cmake make pkgconf \
    && cmake -B build -S . \
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
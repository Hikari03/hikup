FROM alpine:latest
LABEL authors="hikari03"

COPY . /app

WORKDIR /app

# Install dependencies - sodium cmake g++ make
RUN apk add --no-cache pkgconf libsodium-dev cmake g++ make \
    && cmake -B build -S . \
    && cmake --build build --target hikup-server \
    && mv build/hikup-server /app/hikup-server \
    && rm -rf build

EXPOSE 6998

CMD ["/app/hikup-server"]
# Hikup

[![wakatime](https://wakatime.com/badge/user/d150384a-c51c-4144-8898-22213a8a0f55/project/99adba71-871b-4afe-af2c-5039c8030cc8.svg)](https://wakatime.com/badge/user/d150384a-c51c-4144-8898-22213a8a0f55/project/99adba71-871b-4afe-af2c-5039c8030cc8)

![](https://tokei.rs/b1/github/Hikari03/hikup)

### File Sharing Made Easy

- **File Management**: Effortlessly upload, download, and remove files with just one command.
- **Secure Transfer**: All data transfers between the client and server are encrypted to ensure data integrity and privacy. ***( for the moment susceptible to man-in-the-middle attacks)***
- **File Sharing in Three Simple Steps**:
    1. Upload the desired file to the server and receive a unique hash and HTTP link.
    2. Share the generated hash or link with designated recipients.
    3. Recipients can use the hash to download the file directly from the server using the `hikup` command or with HTTP.

> [!NOTE]
> If you append `?view=yes` to the HTTP link, you can view the file directly in the browser.

### Client Commands
- `hikup`: Display help information about the commands.
- `hikup up <file> <server-address>`: Upload a file.
- `hikup down <file> <server-address>`: Download a file.
- `hikup rm <file> <server-address>`: Remove a file.
- `hikup ls <user> <pass> <server-address>`: List all files (requires authentication).

## Dependencies
### Shared
- `cmake`, `libsodium`, `g++` with c++23 support

### Server
- `docker`, `docker-compose` (optional)

## Client
### Build
```bash
git clone https://github.com/Hikari03/hikup.git && \
cd hikup && \
cmake -B build -S . && \
cmake --build build --target hikup -j $(nproc)
```

### Usage
*in hikup/build*
```bash
./hikup # for help
```
## Server

### Settings
- All available runtime settings are in `settings/settings.toml` with descriptions.

### Default Ports
- **Hikup protocol**: 6998
  - If you want to change it, you can do so in `server/ConnectionServer.cpp` and `src/main.cpp`
- **HTTP protocol**: 6997
  - Can be changed in `settings/settings.json`

> [!NOTE]
> When using docker, external ports can be mapped in `docker-compose.yaml`

### Use Docker
#### With scripts
You can get the `docker-update-start.sh`, `docker-attach.sh` and `docker-shutdown.sh`.
With them, you can control the server with ease.
- `docker-update-start.sh` will automatically update the image if needed and start the server
- `docker-attach.sh` will attach you to server console. When you want to exit console, use `CTRL + P, CTRL + Q`.
- `docker-shutdown.sh` will shutdown the container

#### Manually
- download the docker-compose.yml
- `docker compose up -d` or `docker-compose up -d`
- to control the server use `docker attach hikup-server`
- `docker compose down` or `docker-compose down`

### From Source
#### Build
``` bash
git clone https://github.com/Hikari03/hikup.git && \
cd hikup && \
cmake -B build -S . && \
cmake --build build --target hikup-server -j $(nproc)
```
#### Usage:
*in hikup/build*

- once started type `q` to stop the server
``` bash
./hikup-server
```

# Acknowledgements
- [libsodium](https://github.com/jedisct1/libsodium) for encryption
- [mongoose](https://github.com/cesanta/mongoose) for web server
- [toml++](https://github.com/marzer/tomlplusplus) for config parsing

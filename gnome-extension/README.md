# Hikup Files — GNOME Shell extension

Lists the files stored on your hikup servers, straight from the GNOME top panel.

- One submenu per configured server, refreshed every time the menu is opened.
- Each file shows name and size; clicking it copies `hikup down <hash> <server>` to the clipboard.
- The extension talks to the server through the installed `hikup` CLI (`qls`), so no extra
  server-side API is needed.

## Requirements

- `hikup` (client) built from this repository and available in `PATH` — the quiet list
  (`hikup qls <user> <pass> <server>[:port]`) must be supported.
- GNOME Shell 45 or newer.

## Install

```bash
cp -r gnome-extension/hikup-list@hikari03 ~/.local/share/gnome-shell/extensions/
```

Then log out and back in (on X11 you can restart the shell with `Alt+F2`, `r`, `Enter`),
and enable it:

```bash
gnome-extensions enable hikup-list@hikari03
```

## Configuration

Servers are managed in the extension's Settings dialog (menu → Settings…), or by editing
`~/.config/hikup/servers.json` directly:

```json
[
    {
        "name": "My box",
        "address": "hikup.example.org",
        "http": "http://hikup.example.org:6997",
        "user": "admin",
        "pass": "admin"
    }
]
```

`address` uses the same `host[:port]` format as the CLI. `http` is optional — when
empty, HTTP links are built as `http://<host>:6997` (the hikup protocol port is
stripped from `address` first).

## Menu

- One submenu per server; its title shows the file count once loaded.
- A filter box at the top live-filters files (and servers) by name.
- Listings are cached for 60 s: the menu opens instantly, and stale data is
  re-fetched in the background while the cached list stays visible.
- The submenu header shows the server address and total size; failed servers
  show the error with a "Try again" button.
- Files are sorted newest first, each row showing its real file-type icon,
  name, size and upload time.
- Per file row, two glass buttons:
  - the copy button copies `hikup down <hash> <server>` to the clipboard
  - the link button copies the file's HTTP link
- Clicking the row itself opens the file over HTTP in the default browser.

The config file is plain text, so the credentials are stored unencrypted — same as
`settings/settings.toml` on the server.

## Debugging

```bash
gnome-extensions info hikup-list@hikari03
journalctl -f /usr/bin/gnome-shell | grep -i hikup
```

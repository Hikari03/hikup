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
- Per file row, three glass buttons:
  - the copy button copies `hikup down <hash> <server>` to the clipboard
  - the link button copies the file's HTTP link
  - the trash button removes the file (`hikup qrm`) — click once to arm
    (it turns red), click again within 4 s to confirm
- Clicking the row itself opens the file over HTTP in the default browser.

## Uploading

- Each server section starts with "Upload file…": it opens the system file
  picker (via xdg-desktop-portal) and uploads the selection with `hikup qup`.
- The per-server "Verify TLS (vup)" toggle in Settings switches uploads to
  `hikup qvup`, forcing server certificate verification.
- Uploads run in an "Uploads" section at the top of the menu with a live
  glass progress bar (requires a client built with `HIKUP_PROGRESS` support);
  finished uploads show the hash with copy/link buttons, failures show the
  error and a retry button. Uploads keep running while the menu is closed.
- "Clear finished transfers" under the list removes the completed and failed
  entries (running transfers are kept).

## Downloading

- Each file row has a save button (disk icon): it downloads the file with
  `hikup qdown` into GNOME's default download folder (Settings → System →
  Privacy, falling back to `~/Downloads`), showing the same live progress bar.
- Clicking a finished download row opens the local file with its default
  application; the retry button on failures restarts the download.
- Running transfers have a close button next to the percentage: it kills the
  `hikup` process (the server cleans up partial uploads on its own) and
  removes a partially downloaded file.

## Notifications

- When a transfer finishes while the menu is closed, a system notification
  reports the outcome (file name, or the error on failure). Transfers
  finishing with the menu open just update the row in place.
- Completed-transfer notifications carry actions: "Copy link" for uploads
  (or the download command when the server has no HTTP frontend), "Open
  file" and "Open folder" for downloads; clicking the banner does the same
  as the first action.

The config file is plain text, so the credentials are stored unencrypted — same as
`settings/settings.toml` on the server.

## Debugging

```bash
gnome-extensions info hikup-list@hikari03
journalctl -f /usr/bin/gnome-shell | grep -i hikup
```

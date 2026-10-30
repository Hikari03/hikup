import GLib from 'gi://GLib';
import Gio from 'gi://Gio';
import GObject from 'gi://GObject';
import St from 'gi://St';
import Clutter from 'gi://Clutter';
import Graphene from 'gi://Graphene';

import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as PanelMenu from 'resource:///org/gnome/shell/ui/panelMenu.js';
import * as PopupMenu from 'resource:///org/gnome/shell/ui/popupMenu.js';

import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

const CONFIG_PATH = GLib.build_filenamev([GLib.get_user_config_dir(), 'hikup', 'servers.json']);

// cached listings older than this are re-fetched in the background
const CACHE_TTL_MS = 60_000;

function loadServers() {
    try {
        const [ok, contents] = GLib.file_get_contents(CONFIG_PATH);
        if (!ok)
            return [];
        const servers = JSON.parse(new TextDecoder().decode(contents));
        return Array.isArray(servers) ? servers : [];
    } catch (e) {
        return [];
    }
}

function humanSize(bytes) {
    const units = ['B', 'kB', 'MB', 'GB', 'TB'];
    let value = Number(bytes) || 0;
    let unit = 0;
    while (value >= 1024 && unit < units.length - 1) {
        value /= 1024;
        unit++;
    }
    return `${value.toFixed(unit === 0 || value >= 100 ? 0 : 1)} ${units[unit]}`;
}

function timeAgo(epoch) {
    const delta = Math.floor(Date.now() / 1000) - Number(epoch);
    if (!Number.isFinite(delta))
        return '';
    if (delta < 60)
        return 'just now';
    if (delta < 3600)
        return `${Math.floor(delta / 60)} min ago`;
    if (delta < 86400)
        return `${Math.floor(delta / 3600)} h ago`;
    if (delta < 30 * 86400)
        return `${Math.floor(delta / 86400)} d ago`;
    return new Date(Number(epoch) * 1000).toLocaleDateString();
}

// the server's symlink is named <hash><ext> — ".noext" when the file has none
function fileExt(name) {
    const pos = name.lastIndexOf('.');
    return pos > 0 ? name.slice(pos) : '.noext';
}

// HTTP base of the server: the optional "http" field wins, otherwise
// http://<host>:6997 (hikup protocol port stripped from the address)
function httpBase(server) {
    if (server.http)
        return String(server.http).replace(/\/+$/, '');
    let host = String(server.address ?? '');
    const pos = host.lastIndexOf(':');
    if (pos > 0)
        host = host.slice(0, pos);
    return `http://${host}:6997`;
}

function serverKey(server) {
    return `${String(server.address ?? '')}|${String(server.user ?? '')}`;
}

function serverLabel(server) {
    return server.name || server.address || 'server';
}

// real file-type icon (pdf, image, archive, …) for a filename
function fileGicon(name) {
    try {
        const [contentType] = Gio.content_type_guess(name, null);
        return Gio.content_type_get_icon(contentType);
    } catch (e) {
        return null;
    }
}

function parseFiles(stdout) {
    return String(stdout ?? '')
        .split('\n')
        .filter(line => line.length > 0)
        .map(line => {
            const [name, size, uploaded, hash] = line.split('\t');
            return {name, size, uploaded, hash};
        })
        .sort((a, b) => Number(b.uploaded ?? 0) - Number(a.uploaded ?? 0));   // newest first
}

const HikupButton = GObject.registerClass(
class HikupButton extends PanelMenu.Button {
    _init(extension) {
        super._init(0.5, 'Hikup servers');

        this._extension = extension;

        const box = new St.BoxLayout({style_class: 'panel-status-menu-box'});
        box.add_child(new St.Icon({
            icon_name: 'folder-download-symbolic',
            style_class: 'system-status-icon',
        }));
        box.add_child(new St.Label({
            text: 'hikup',
            y_align: Clutter.ActorAlign.CENTER,
            style_class: 'hikup-panel-label',
        }));
        this.add_child(box);
        this.add_style_class_name('hikup-panel-button');
        this.menu.actor?.add_style_class_name('hikup-menu');

        // per-server listing cache: {state: 'loading'|'done'|'error', …}
        this._cache = new Map();
        this._filterText = '';
        this._fileRows = [];    // {item, section, text}
        this._sections = [];    // {sub, server}

        this.menu.connect('open-state-changed', (menu, open) => {
            if (open)
                this._rebuild();
        });

        // PopupMenu refuses to open an empty menu, so the menu must be
        // populated before the first click; on-open rebuilds only refresh it
        this._rebuild();
    }

    _rebuild() {
        this.menu.removeAll();
        this._fileRows = [];
        this._sections = [];

        const servers = loadServers();

        // ── filter box ─────────────────────────────────────────────────────
        const entry = new St.Entry({
            hint_text: 'Filter files…',
            can_focus: true,
            track_hover: true,
            x_expand: true,
            style_class: 'hikup-filter-entry',
        });
        entry.text = this._filterText;
        entry.clutter_text.connect('text-changed', () => {
            this._filterText = entry.clutter_text.text;
            this._applyFilter();
        });
        const filterItem = new PopupMenu.PopupBaseMenuItem({
            reactive: false,
            activate: false,
            hover: false,
        });
        filterItem.add_style_class_name('hikup-filter-item');
        filterItem.add_child(entry);
        this.menu.addMenuItem(filterItem);

        // ── server sections ────────────────────────────────────────────────
        if (servers.length === 0) {
            const hint = new PopupMenu.PopupMenuItem('No servers configured — add one in Settings');
            hint.sensitive = false;
            this.menu.addMenuItem(hint);
        }

        for (const server of servers) {
            const sub = new PopupMenu.PopupSubMenuMenuItem(serverLabel(server));
            sub.add_style_class_name('hikup-server-item');
            this.menu.addMenuItem(sub);

            const section = {sub, server};
            this._sections.push(section);

            const cached = this._cache.get(serverKey(server));

            if (cached?.state === 'done') {
                this._renderFiles(section, cached.files);
                // stale-while-revalidate: show the cached list, refresh quietly
                if (Date.now() - cached.when > CACHE_TTL_MS)
                    this._fetch(server, sub, true);
            } else if (cached?.state === 'loading') {
                this._showSpinner(sub);
            } else if (cached?.state === 'error') {
                this._showError(sub, server, cached.error);
            } else {
                this._fetch(server, sub);
            }
        }

        this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());

        const refresh = new PopupMenu.PopupMenuItem('Refresh');
        refresh.connect('activate', () => {
            this._cache.clear();
            this._rebuild();
        });
        this.menu.addMenuItem(refresh);

        const settings = new PopupMenu.PopupMenuItem('Settings…');
        settings.connect('activate', () => this._extension.openPreferences());
        this.menu.addMenuItem(settings);

        this._applyFilter();
    }

    // ── fetch ──────────────────────────────────────────────────────────────
    _fetch(server, sub, background = false) {
        const key = serverKey(server);
        this._cache.set(key, {state: 'loading'});
        if (!background)
            this._showSpinner(sub);

        // qls = quiet list: one line per file: name \t size \t epoch \t hash
        // resolve hikup against the PATH gnome-shell sees, not the user's shell
        const hikup = GLib.find_program_in_path('hikup');
        if (!hikup) {
            const error = 'hikup not found in the PATH of gnome-shell (symlink it into /usr/local/bin)';
            this._cache.set(key, {state: 'error', error, when: Date.now()});
            this._showError(sub, server, error);
            return;
        }

        const argv = [
            hikup, 'qls',
            String(server.user ?? ''),
            String(server.pass ?? ''),
            String(server.address ?? ''),
        ];

        let proc;
        try {
            proc = Gio.Subprocess.new(argv,
                Gio.SubprocessFlags.STDOUT_PIPE | Gio.SubprocessFlags.STDERR_PIPE);
        } catch (e) {
            const error = `could not start hikup: ${e.message}`;
            this._cache.set(key, {state: 'error', error, when: Date.now()});
            this._showError(sub, server, error);
            return;
        }

        // watchdog: a hung hikup (e.g. unreachable server) must not spin forever
        let finished = false;
        const timeoutId = GLib.timeout_add_seconds(GLib.PRIORITY_DEFAULT, 20, () => {
            if (!finished) {
                finished = true;
                proc.force_exit();
                const error = `timeout — try 'hikup qls … ${server.address}' in a terminal`;
                this._cache.set(key, {state: 'error', error, when: Date.now()});
                this._showError(sub, server, error);
            }
            return GLib.SOURCE_REMOVE;
        });

        proc.communicate_utf8_async(null, null, (p, res) => {
            if (finished)
                return;
            finished = true;
            GLib.source_remove(timeoutId);

            let files = null;
            let error = null;
            try {
                const [, out, err] = p.communicate_utf8_finish(res);
                if (p.get_exit_status() === 0) {
                    files = parseFiles(out);
                } else {
                    // first line of stderr is hikup's error message
                    error = err.trim().split('\n')[0] || `hikup exited with code ${p.get_exit_status()}`;
                }
            } catch (e) {
                error = e.message;
            }

            if (files) {
                this._cache.set(key, {state: 'done', files, when: Date.now()});
                // the section may have been rebuilt away while we were fetching
                const section = this._sections.find(s => s.server === server);
                if (section)
                    this._renderFiles(section, files);
            } else {
                this._cache.set(key, {state: 'error', error, when: Date.now()});
                this._showError(sub, server, error ?? 'unknown error');
            }
        });
    }

    // ── section states ─────────────────────────────────────────────────────
    _clearSub(sub) {
        try {
            sub.menu.removeAll();
        } catch (e) {
            return false;   // submenu was destroyed meanwhile (menu rebuilt)
        }
        return true;
    }

    _showSpinner(sub) {
        if (!this._clearSub(sub))
            return;

        const item = new PopupMenu.PopupBaseMenuItem({reactive: false, activate: false});
        const box = new St.BoxLayout({style: 'spacing: 8px;'});

        const spinner = new St.Icon({
            icon_name: 'process-working-symbolic',
            style_class: 'hikup-spinner',
        });
        spinner.pivot_point = new Graphene.Point({x: 0.5, y: 0.5});
        spinner.ease({
            rotation_angle_z: 360,
            duration: 1000,
            mode: Clutter.AnimationMode.LINEAR,
            repeat_count: -1,
        });

        box.add_child(spinner);
        box.add_child(new St.Label({text: 'loading…', y_align: Clutter.ActorAlign.CENTER}));
        item.add_child(box);
        sub.menu.addMenuItem(item);
    }

    _showError(sub, server, message) {
        if (!this._clearSub(sub))
            return;

        const error = new PopupMenu.PopupMenuItem(`error: ${message}`);
        error.sensitive = false;
        error.add_style_class_name('hikup-error-item');
        sub.menu.addMenuItem(error);

        const retryItem = new PopupMenu.PopupBaseMenuItem({reactive: false, activate: false, hover: false});
        const retry = new St.Button({
            label: 'Try again',
            style_class: 'button hikup-retry-button',
            can_focus: true,
        });
        retry.connect('clicked', () => this._fetch(server, sub));
        retryItem.add_child(retry);
        sub.menu.addMenuItem(retryItem);
    }

    _renderFiles(section, files) {
        const {sub, server} = section;
        if (!this._clearSub(sub))
            return;

        if (files.length === 0) {
            const item = new PopupMenu.PopupMenuItem('no files');
            item.sensitive = false;
            sub.menu.addMenuItem(item);
            sub.label?.set_text(serverLabel(server));
            return;
        }

        // ── server info header ────────────────────────────────────────────
        const totalBytes = files.reduce((sum, file) => sum + (Number(file.size) || 0), 0);
        sub.label?.set_text(`${serverLabel(server)} (${files.length})`);

        const info = new PopupMenu.PopupMenuItem(
            `${server.address ?? ''}  ·  ${humanSize(totalBytes)} total`);
        info.sensitive = false;
        info.add_style_class_name('hikup-info-item');
        sub.menu.addMenuItem(info);

        sub.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());

        // ── file rows ─────────────────────────────────────────────────────
        for (const file of files) {
            const name = file.name.length > 42 ? `${file.name.slice(0, 41)}…` : file.name;
            const command = `hikup down ${file.hash} ${server.address}`;
            const httpLink = `${httpBase(server)}/${file.hash}${fileExt(file.name)}`;

            const item = new PopupMenu.PopupBaseMenuItem();
            item.add_style_class_name('hikup-file-item');

            const gicon = fileGicon(file.name);
            item.add_child(gicon
                ? new St.Icon({gicon, style_class: 'hikup-file-icon'})
                : new St.Icon({icon_name: 'text-x-generic-symbolic', style_class: 'hikup-file-icon'}));

            const nameLabel = new St.Label({
                text: name,
                x_expand: true,
                style_class: 'hikup-file-name',
            });
            item.add_child(nameLabel);

            const metaLabel = new St.Label({
                text: `${humanSize(file.size)}  ·  ${timeAgo(file.uploaded)}`,
                style_class: 'hikup-file-meta',
            });
            item.add_child(metaLabel);

            item.add_child(this._actionButton('edit-copy-symbolic',
                () => this._copy(command)));
            item.add_child(this._actionButton('insert-link-symbolic',
                () => this._copy(httpLink)));

            // clicking the row itself opens the file over HTTP
            item.connect('activate', () => this._openUri(httpLink));
            sub.menu.addMenuItem(item);

            this._fileRows.push({
                item,
                section,
                text: `${file.name} ${serverLabel(server)}`.toLowerCase(),
            });
        }
    }

    // ── filter ─────────────────────────────────────────────────────────────
    _applyFilter() {
        const query = this._filterText.trim().toLowerCase();

        for (const row of this._fileRows)
            row.item.visible = !query || row.text.includes(query);

        for (const section of this._sections) {
            if (!query) {
                section.sub.visible = true;
                continue;
            }
            const rows = this._fileRows.filter(row => row.section === section);
            const anyVisible = rows.some(row => row.item.visible);
            const nameMatches = serverLabel(section.server).toLowerCase().includes(query);
            section.sub.visible = anyVisible || nameMatches;
        }
    }

    // ── helpers ────────────────────────────────────────────────────────────
    _actionButton(iconName, onClicked) {
        const button = new St.Button({
            can_focus: true,
            style_class: 'hikup-action-button',
            child: new St.Icon({
                icon_name: iconName,
                style_class: 'hikup-action-icon',
            }),
        });
        button.connect('clicked', onClicked);
        return button;
    }

    _copy(text) {
        St.Clipboard.get_default().set_text(St.ClipboardType.CLIPBOARD, text);
    }

    _openUri(uri) {
        try {
            Gio.AppInfo.launch_default_for_uri(uri, null);
        } catch (e) {
            this._copy(uri);   // fallback: at least put the link on the clipboard
        }
    }
});

export default class HikupExtension extends Extension {
    enable() {
        this._button = new HikupButton(this);
        Main.panel.addToStatusArea('hikup', this._button);
    }

    disable() {
        this._button?.destroy();
        this._button = null;
    }
}

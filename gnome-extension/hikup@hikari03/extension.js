import GLib from 'gi://GLib';
import Gio from 'gi://Gio';
import GObject from 'gi://GObject';
import St from 'gi://St';
import Clutter from 'gi://Clutter';
import Graphene from 'gi://Graphene';
import Cairo from 'cairo';

import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as MessageTray from 'resource:///org/gnome/shell/ui/messageTray.js';
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

// GNOME's default download folder (settable in Settings → Privacy or via
// XDG user dirs), falling back to ~/Downloads
function downloadDir() {
    const dir = GLib.get_user_special_dir(GLib.UserDirectory.DIRECTORY_DOWNLOAD);
    return dir || GLib.build_filenamev([GLib.get_home_dir(), 'Downloads']);
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

// ── aero glass progress bar, drawn with Cairo ─────────────────────────────
const HikupProgressBar = GObject.registerClass(
class HikupProgressBar extends St.DrawingArea {
    _init(params = {}) {
        super._init(params);
        this._fraction = 0;
        this.connect('repaint', this._onRepaint.bind(this));
    }

    setFraction(fraction) {
        this._fraction = Math.max(0.0, Math.min(1.0, fraction));
        this.queue_repaint();
    }

    _roundedRect(cr, x, y, w, h) {
        const r = h / 2;
        cr.newSubPath();
        cr.arc(x + w - r, y + r, r, -Math.PI / 2, 0);
        cr.arc(x + w - r, y + h - r, r, 0, Math.PI / 2);
        cr.arc(x + r, y + h - r, r, Math.PI / 2, Math.PI);
        cr.arc(x + r, y + r, r, Math.PI, 1.5 * Math.PI);
        cr.closePath();
    }

    _onRepaint(area) {
        const cr = area.get_context();
        const [width, height] = area.get_surface_size();
        if (width <= 0 || height <= 0)
            return;

        // frosted track
        cr.setOperator(Cairo.Operator.CLEAR);
        cr.paint();
        cr.setOperator(Cairo.Operator.OVER);
        cr.setSourceRGBA(0.22, 0.36, 0.45, 0.45);
        this._roundedRect(cr, 0, 0, width, height);
        cr.fill();

        // glass gradient fill with a specular sheen on the leading edge
        const fillWidth = Math.max(height, this._fraction * width);
        const gradient = new Cairo.LinearGradient(0, 0, fillWidth, 0);
        gradient.addColorStopRGBA(0.0, 14 / 255, 165 / 255, 233 / 255, 1.0);   // deep aero blue
        gradient.addColorStopRGBA(0.85, 56 / 255, 189 / 255, 248 / 255, 1.0); // aero sky
        gradient.addColorStopRGBA(1.0, 1.0, 1.0, 1.0, 0.9);                   // sheen
        cr.setSource(gradient);
        this._roundedRect(cr, 0, 0, fillWidth, height);
        cr.fill();
    }
});

const HikupButton = GObject.registerClass(
class HikupButton extends PanelMenu.Button {
    _init(extension) {
        super._init(0.5, 'Hikup servers');

        this._extension = extension;

        // St.BoxLayout ignores per-child layout properties on GNOME 51 (the
        // upload rows hit the same thing), and a stacked box packs the icon
        // toward the top once the button stretches its child — a BinLayout
        // instead keeps the icon dead-center and floats the bar below it
        const box = new St.Widget({
            layout_manager: new Clutter.BinLayout(),
            style_class: 'panel-status-menu-box',
        });
        box.add_child(new St.Icon({
            icon_name: 'weather-overcast-symbolic',
            style_class: 'system-status-icon',
            x_align: Clutter.ActorAlign.CENTER,
            y_align: Clutter.ActorAlign.CENTER,
        }));
        this.add_child(box);
        this._panelBox = box;

        // thin aggregate progress bar under the icon — added to the box only
        // while transfers run, so it takes no space when idle. The box is only
        // as tall as the icon, so END alone would pin the bar inside it; the
        // translation drops it to just below the icon's bottom edge
        this._panelBar = new HikupProgressBar({
            width: 20,
            height: 3,
            x_align: Clutter.ActorAlign.CENTER,
            y_align: Clutter.ActorAlign.END,
            translation_y: 8,
            style_class: 'hikup-panel-progress-bar',
        });
        this.add_style_class_name('hikup-panel-button');
        this.menu.actor?.add_style_class_name('hikup-menu');

        // take the notification source down with the indicator
        this.connect('destroy', () => this._notificationSource?.destroy());

        // per-server listing cache: {state: 'loading'|'done'|'error', …}
        this._cache = new Map();
        // in-flight and finished uploads, newest first
        this._uploads = [];
        this._filterText = '';
        this._fileRows = [];    // {item, section, text}
        this._sections = [];    // {sub, server}
        this._notificationSource = null;
        this._urlEntryKey = null;   // serverKey of the section with an open URL entry
        this._rebuildPending = false;

        this.menu.connect('open-state-changed', (menu, open) => {
            if (open)
                this._rebuild();
        });

        // PopupMenu refuses to open an empty menu, so the menu must be
        // populated before the first click; on-open rebuilds only refresh it
        this._rebuild();
    }

    // ── rebuild ─────────────────────────────────────────────────────────────
    // never mutate the menu synchronously from a click/key handler —
    // destroying the actor under the pointer makes the menu grab give up and
    // the menu closes. Rebuild on the next idle instead, coalesced.
    _rebuild() {
        if ( this._rebuildPending )
            return;
        this._rebuildPending = true;

        GLib.idle_add(GLib.PRIORITY_DEFAULT, () => {
            this._rebuildPending = false;
            try {
                this._rebuildNow();
            } catch (e) {
            }
            return GLib.SOURCE_REMOVE;
        });
    }

    _rebuildNow() {
        // remember which server submenus were expanded — the rebuild replaces
        // them with fresh (collapsed) ones and must restore their state
        const openKeys = new Set(this._sections
            .filter(section => section.sub.menu.isOpen)
            .map(section => serverKey(section.server)));

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

        // ── uploads ─────────────────────────────────────────────────────────
        for (const upload of this._uploads)
            this.menu.addMenuItem(this._uploadRow(upload));
        if (this._uploads.some(upload => upload.state !== 'running')) {
            const clearItem = new PopupMenu.PopupBaseMenuItem({reactive: false, activate: false, hover: false});
            clearItem.add_style_class_name('hikup-clear-item');
            const clear = new St.Button({
                label: 'Clear finished transfers',
                style_class: 'button hikup-retry-button',
                can_focus: true,
                x_align: Clutter.ActorAlign.CENTER,
            });
            clear.connect('clicked', () => {
                this._uploads = this._uploads.filter(upload => upload.state === 'running');
                this._rebuild();
            });
            clearItem.add_child(clear);
            this.menu.addMenuItem(clearItem);
        }
        if (this._uploads.length > 0)
            this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());

        // ── server sections ────────────────────────────────────────────────
        if (servers.length === 0) {
            const hint = new PopupMenu.PopupMenuItem('No servers configured — add one in Settings');
            hint.sensitive = false;
            this.menu.addMenuItem(hint);
        }

        for (const server of servers) {
            const sub = new PopupMenu.PopupSubMenuMenuItem(serverLabel(server));
            sub.add_style_class_name('hikup-server-item');
            if ( openKeys.has(serverKey(server)) )
                sub.setSubmenuShown(true);
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
                this._showSpinner(sub, server);
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

        // submenus re-expand reliably only once the menu has been laid out —
        // setSubmenuShown above runs during construction, so do it once more
        // on the next idle for anything that did not stick
        if ( openKeys.size > 0 ) {
            GLib.idle_add(GLib.PRIORITY_DEFAULT, () => {
                for ( const section of this._sections )
                    if ( openKeys.has(serverKey(section.server)) && !section.sub.menu.isOpen )
                        try {
                            section.sub.setSubmenuShown(true);
                        } catch (e) {
                        }
                return GLib.SOURCE_REMOVE;
            });
        }
    }

    // ── fetch ──────────────────────────────────────────────────────────────
    _fetch(server, sub, background = false) {
        const key = serverKey(server);
        this._cache.set(key, {state: 'loading'});
        if (!background)
            this._showSpinner(sub, server);

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

    // "Upload file…" / "Transfer URL…" are the first entries of every server
    // section regardless of which state it renders in
    _sectionPreamble(sub, server) {
        const key = serverKey(server);

        // URL entry mode: type/paste a URL, Enter fetches it server-side
        if ( this._urlEntryKey === key ) {
            const item = new PopupMenu.PopupBaseMenuItem({reactive: false, activate: false, hover: false});
            item.add_style_class_name('hikup-filter-item');

            const entry = new St.Entry({
                hint_text: 'URL to fetch (http/https)…',
                can_focus: true,
                track_hover: true,
                x_expand: true,
                style_class: 'hikup-filter-entry',
            });
            const text = entry.clutter_text;
            text.connect('activate', () => {
                const url = text.text.trim();
                this._urlEntryKey = null;
                if ( url )
                    this._startTransfer(server, url);
                if ( this.menu.isOpen )
                    this._rebuild();
            });
            text.connect('key-press-event', (t, event) => {
                if ( event.get_key_symbol() === Clutter.KEY_Escape ) {
                    this._urlEntryKey = null;
                    GLib.idle_add(GLib.PRIORITY_DEFAULT, () => {
                        this._rerenderSection(sub, server);
                        return GLib.SOURCE_REMOVE;
                    });
                }
                return Clutter.EVENT_PROPAGATE;
            });

            item.add_child(entry);
            sub.menu.addMenuItem(item);
            sub.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());

            GLib.idle_add(GLib.PRIORITY_DEFAULT, () => {
                try {
                    text.grab_key_focus();
                } catch (e) {
                }
                return GLib.SOURCE_REMOVE;
            });
            return;
        }

        const item = new PopupMenu.PopupBaseMenuItem({reactive: false, activate: false, hover: false});
        item.add_style_class_name('hikup-preamble-item');

        const upload = new St.Button({
            label: 'Upload file…',
            style_class: 'button hikup-retry-button',
            can_focus: true,
        });
        upload.connect('clicked', () => this._pickFile(server));

        const transfer = new St.Button({
            label: 'Transfer URL…',
            style_class: 'button hikup-retry-button',
            can_focus: true,
        });
        transfer.connect('clicked', () => {
            this._urlEntryKey = key;
            // re-render only this section (deferred, past the click event) —
            // the submenu itself is never destroyed, so it stays open
            GLib.idle_add(GLib.PRIORITY_DEFAULT, () => {
                this._rerenderSection(sub, server);
                return GLib.SOURCE_REMOVE;
            });
        });

        item.add_child(upload);
        item.add_child(transfer);
        sub.menu.addMenuItem(item);
        sub.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
    }

    // re-render a single server section in place (keeps the submenu open,
    // unlike a full rebuild which replaces it with a collapsed one)
    _rerenderSection(sub, server) {
        const cached = this._cache.get(serverKey(server));

        if ( cached?.state === 'done' ) {
            const section = this._sections.find(s => s.sub === sub);
            if ( section )
                this._renderFiles(section, cached.files);
        } else if ( cached?.state === 'loading' )
            this._showSpinner(sub, server);
        else if ( cached?.state === 'error' )
            this._showError(sub, server, cached.error);
        else
            this._fetch(server, sub);

        this._applyFilter();
    }

    _showSpinner(sub, server) {
        if (!this._clearSub(sub))
            return;
        this._sectionPreamble(sub, server);

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
        this._sectionPreamble(sub, server);

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
        this._sectionPreamble(sub, server);

        // rows from a previous render of this section are stale — drop them so
        // filtering and counting only ever see live actors
        this._fileRows = this._fileRows.filter(row => row.section !== section);

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
            item.add_child(this._actionButton('document-save-symbolic',
                () => this._startDownload(file, server)));
            item.add_child(this._removeButton(file, server, section));

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

    // ── uploads ────────────────────────────────────────────────────────────
    _pickFile(server) {
        // native file dialog via xdg-desktop-portal (no extra dependencies)
        // request path: .../request/<SENDER>/<TOKEN>, where SENDER is the
        // unique bus name minus the leading ':' and with '.' replaced by '_'
        const token = `hikup${Date.now()}`;
        const sender = Gio.DBus.session.get_unique_name().slice(1).replace(/\./g, '_');
        const requestPath = `/org/freedesktop/portal/desktop/request/${sender}/${token}`;

        const handlerId = Gio.DBus.session.signal_subscribe(
            'org.freedesktop.portal.Desktop', 'org.freedesktop.portal.Request', 'Response',
            requestPath, null, Gio.DBusSignalFlags.NONE,
            (conn, name, path, iface, signal, params) => {
                Gio.DBus.session.signal_unsubscribe(handlerId);
                const [code, results] = params.deepUnpack();
                if (code !== 0)
                    return;   // cancelled
                const uris = results.uris ? results.uris.deepUnpack() : [];
                for (const uri of uris.slice(0, 10))
                    this._startUpload(server, uri);
            });

        Gio.DBus.session.call(
            'org.freedesktop.portal.Desktop', '/org/freedesktop/portal/desktop',
            'org.freedesktop.portal.FileChooser', 'OpenFile',
            new GLib.Variant('(ssa{sv})', ['', 'Upload to hikup', {
                handle_token: new GLib.Variant('s', token),
                multiple: new GLib.Variant('b', true),
            }]),
            new GLib.VariantType('(o)'),
            Gio.DBusCallFlags.NONE, -1, null,
            (bus, res) => {
                try {
                    bus.call_finish(res);
                } catch (e) {
                    Gio.DBus.session.signal_unsubscribe(handlerId);
                    Main.notify('hikup', `could not open the file picker: ${e.message}`);
                }
            });
    }

    _startUpload(server, uri) {
        const hikup = GLib.find_program_in_path('hikup');
        if (!hikup) {
            Main.notify('hikup', 'hikup not found in the PATH of gnome-shell');
            return;
        }

        let path = null;
        try {
            [path] = GLib.filename_from_uri(uri);
        } catch (e) {
            return;
        }
        if (!path)
            return;

        let size = 0;
        try {
            size = Gio.File.new_for_path(path)
                .query_info('standard::size', Gio.FileQueryInfoFlags.NONE, null)
                .get_size();
        } catch (e) {
        }

        const entry = {
            kind: 'upload',
            name: GLib.path_get_basename(path),
            path,
            size,
            server,
            state: 'running',
            sent: 0,
            total: size,
            hash: null,
            link: null,
            error: null,
            stderr: [],
            _bar: null,
            _pct: null,
        };
        this._uploads.unshift(entry);

        // qup = quiet upload, HIKUP_PROGRESS=1 streams "PROGRESS <sent> <total>"
        // lines; the "verify TLS" setting selects vup semantics
        const argv = [hikup, server.verify ? 'qvup' : 'qup', path, String(server.address ?? '')];

        let proc;
        try {
            const launcher = new Gio.SubprocessLauncher({
                flags: Gio.SubprocessFlags.STDOUT_PIPE | Gio.SubprocessFlags.STDERR_PIPE,
            });
            launcher.setenv('HIKUP_PROGRESS', '1', true);
            proc = launcher.spawnv(argv);
            entry.proc = proc;   // kept so the transfer can be cancelled
        } catch (e) {
            entry.state = 'error';
            entry.error = e.message;
            return;
        }

        // stream stdout: progress lines plus the final "hash:" / "http:"
        this._readLines(proc.get_stdout_pipe(), line => this._onUploadLine(entry, line), () => {
            proc.wait_async(null, p => this._finishUpload(entry, p));
        });

        // collect stderr for error reporting
        this._readLines(proc.get_stderr_pipe(), line => entry.stderr.push(line), () => {});

        // the menu stays open (button click) — show the running row now
        this._updatePanelBar();
        if (this.menu.isOpen)
            this._rebuild();
    }

    _startTransfer(server, url) {
        const hikup = GLib.find_program_in_path('hikup');
        if (!hikup) {
            Main.notify('hikup', 'hikup not found in the PATH of gnome-shell');
            return;
        }

        // display name from the URL path, mirroring the server's logic
        let name = 'index.html';
        {
            let pathPart = url;
            const scheme = pathPart.indexOf('://');
            if ( scheme >= 0 )
                pathPart = pathPart.substring(scheme + 3);
            const slash = pathPart.indexOf('/');
            if ( slash >= 0 ) {
                pathPart = pathPart.substring(slash + 1);
                const q = pathPart.search(/[?#]/);
                if ( q >= 0 )
                    pathPart = pathPart.substring(0, q);
                if ( pathPart )
                    name = pathPart.split('/').pop();
            }
        }

        const entry = {
            kind: 'transfer',
            name,
            url,
            server,
            state: 'running',
            sent: 0,
            total: 0,
            hash: null,
            link: null,
            error: null,
            stderr: [],
            _bar: null,
            _pct: null,
        };
        this._uploads.unshift(entry);

        // qtransfer streams "PROGRESS <now> <total>" lines while the server
        // fetches, then "hash:" / "http:" like an upload
        const argv = [
            hikup,
            server.verify ? 'qvtransfer' : 'qtransfer',
            url,
            String(server.address ?? ''),
        ];

        let proc;
        try {
            const launcher = new Gio.SubprocessLauncher({
                flags: Gio.SubprocessFlags.STDOUT_PIPE | Gio.SubprocessFlags.STDERR_PIPE,
            });
            proc = launcher.spawnv(argv);
            entry.proc = proc;   // kept so the transfer can be cancelled
        } catch (e) {
            entry.state = 'error';
            entry.error = e.message;
            return;
        }

        this._readLines(proc.get_stdout_pipe(), line => this._onUploadLine(entry, line), () => {
            proc.wait_async(null, p => this._finishUpload(entry, p));
        });
        this._readLines(proc.get_stderr_pipe(), line => entry.stderr.push(line), () => {});

        this._updatePanelBar();
        if (this.menu.isOpen)
            this._rebuild();
    }

    _startDownload(file, server) {
        const hikup = GLib.find_program_in_path('hikup');
        if (!hikup) {
            Main.notify('hikup', 'hikup not found in the PATH of gnome-shell');
            return;
        }

        const dir = downloadDir();
        GLib.mkdir_with_parents(dir, 0o755);

        const entry = {
            kind: 'download',
            name: file.name,
            hash: String(file.hash),
            path: GLib.build_filenamev([dir, file.name]),
            size: Number(file.size) || 0,
            server,
            state: 'running',
            sent: 0,
            total: Number(file.size) || 0,
            error: null,
            stderr: [],
            _bar: null,
            _pct: null,
        };
        this._uploads.unshift(entry);

        // qdown = quiet download into the given directory,
        // HIKUP_PROGRESS=1 streams "PROGRESS <got> <total>" lines
        const argv = [
            hikup,
            server.verify ? 'qvdown' : 'qdown',
            String(file.hash),
            String(server.address ?? ''),
            dir,
        ];

        let proc;
        try {
            const launcher = new Gio.SubprocessLauncher({
                flags: Gio.SubprocessFlags.STDOUT_PIPE | Gio.SubprocessFlags.STDERR_PIPE,
            });
            launcher.setenv('HIKUP_PROGRESS', '1', true);
            proc = launcher.spawnv(argv);
            entry.proc = proc;   // kept so the transfer can be cancelled
        } catch (e) {
            entry.state = 'error';
            entry.error = e.message;
            return;
        }

        // stream stdout: progress lines (downloads print nothing else in quiet mode)
        this._readLines(proc.get_stdout_pipe(), line => this._onUploadLine(entry, line), () => {
            proc.wait_async(null, p => this._finishUpload(entry, p));
        });

        // collect stderr for error reporting
        this._readLines(proc.get_stderr_pipe(), line => entry.stderr.push(line), () => {});

        // the menu stays open when a download starts (the button does not
        // close it) — rebuild so the running row and its bar appear now
        this._updatePanelBar();
        if (this.menu.isOpen)
            this._rebuild();
    }

    _readLines(stream, onLine, onEof) {
        const dataStream = new Gio.DataInputStream({base_stream: stream});
        const readNext = () => {
            dataStream.read_line_async(GLib.PRIORITY_DEFAULT, null, (s, res) => {
                let data;
                try {
                    [data] = s.read_line_finish(res);
                } catch (e) {
                    onEof();
                    return;
                }
                if (data === null) {
                    try {
                        s.close(null);
                    } catch (e) {
                    }
                    onEof();
                    return;
                }
                onLine(new TextDecoder().decode(data));
                readNext();
            });
        };
        readNext();
    }

    _onUploadLine(entry, line) {
        if (line.startsWith('PROGRESS ')) {
            const parts = line.split(' ');
            const sent = Number(parts[1]) || 0;
            const total = Number(parts[2]) || 0;
            entry.sent = sent;
            entry.total = total || entry.total;
            const fraction = entry.total ? sent / entry.total : 1;
            try {
                entry._bar?.setFraction(fraction);
            } catch (e) {
            }
            try {
                entry._pct?.set_text(`${Math.round(fraction * 100)}%`);
            } catch (e) {
            }
            this._updatePanelBar();
        } else if (line.startsWith('hash: ')) {
            entry.hash = line.slice('hash: '.length).trim();
        } else if (line.startsWith('http: ')) {
            entry.link = line.slice('http: '.length).trim();
        }
    }

    _cancelTransfer(entry) {
        entry.cancelled = true;
        entry.proc?.force_exit();
    }

    _finishUpload(entry, proc) {
        if (entry.cancelled) {
            // user-initiated: drop the row, remove a partial download, no notification
            this._uploads = this._uploads.filter(upload => upload !== entry);
            if (entry.kind === 'download' && entry.total && entry.sent < entry.total) {
                try {
                    Gio.File.new_for_path(entry.path).delete(null);
                } catch (e) {
                }
            }
            if (this.menu.isOpen)
                this._rebuild();
            this._updatePanelBar();
            return;
        }

        const code = proc.get_exit_status();
        if (code === 0 && (entry.hash || entry.kind === 'download')) {
            entry.state = 'done';
            if (entry.kind !== 'download')
                this._cache.delete(serverKey(entry.server));   // listing is stale now
        } else {
            entry.state = 'error';
            entry.error = entry.stderr.filter(line => line.length > 0).pop()
                ?? `hikup exited with code ${code}`;
        }
        entry._bar = null;
        entry._pct = null;

        if (this.menu.isOpen)
            this._rebuild();
        else
            this._notifyFinished(entry);   // user is not watching the menu
        this._updatePanelBar();
    }

    _notifyFinished(entry) {
        const kindLabel = entry.kind === 'download' ? 'download' : 'upload';

        if (entry.state !== 'done') {
            Main.notify(`hikup — ${kindLabel} failed`,
                `${entry.name}: ${entry.error ?? 'unknown error'}`);
            return;
        }

        try {
            const source = this._getNotificationSource();

            const notification = new MessageTray.Notification({
                source,
                title: `hikup — ${kindLabel} complete`,
                body: entry.name,
                iconName: 'weather-overcast-symbolic',
            });

            if (entry.kind === 'download') {
                // clicking the banner (or "Open file") opens the file,
                // "Open folder" its containing directory in the file manager
                const openFile = () => {
                    try {
                        this._openUri(GLib.filename_to_uri(entry.path, null));
                    } catch (e) {
                        this._copy(entry.path);
                    }
                };
                notification.connect('activated', openFile);
                notification.addAction('Open file', openFile);
                notification.addAction('Open folder', () => {
                    try {
                        this._openUri(
                            GLib.filename_to_uri(GLib.path_get_dirname(entry.path), null));
                    } catch (e) {
                    }
                });
            } else {
                if (entry.link)
                    notification.connect('activated', () => this._openUri(entry.link));
                notification.addAction('Copy link', () => {
                    if (entry.link)
                        this._copy(entry.link);
                    else   // no HTTP frontend on this server: copy the command
                        this._copy(`hikup down ${entry.hash} ${entry.server.address}`);
                });
            }

            source.addNotification(notification);
        } catch (e) {
            // safety net against API drift: plain notification, no buttons
            Main.notify(`hikup — ${kindLabel} complete`, entry.name);
        }
    }

    _getNotificationSource() {
        if (!this._notificationSource) {
            this._notificationSource = new MessageTray.Source({
                title: 'hikup',
                iconName: 'weather-overcast-symbolic',
            });
            this._notificationSource.connect('destroy', () => {
                this._notificationSource = null;
            });
            Main.messageTray.add(this._notificationSource);
        }
        return this._notificationSource;
    }

    _uploadRow(entry) {
        if (entry.state === 'running') {
            const item = new PopupMenu.PopupBaseMenuItem({reactive: false, activate: false, hover: false});
            item.add_style_class_name('hikup-upload-item');

            // two lines: name + percentage on top, the bar underneath at full
            // width — a long file name must never shrink the bar
            // (St.BoxLayout has no layout properties on GNOME 51, so the
            // vertical container is a St.Widget with a layout manager)
            const box = new St.Widget({
                layout_manager: new Clutter.BoxLayout({
                    orientation: Clutter.Orientation.VERTICAL,
                    spacing: 4,
                }),
                x_expand: true,
            });

            const header = new St.BoxLayout({x_expand: true, style: 'spacing: 8px;'});
            header.add_child(new St.Label({
                text: entry.name,
                x_expand: true,
                style_class: 'hikup-file-name',
            }));

            const fraction = entry.total ? entry.sent / entry.total : 0;
            const pct = new St.Label({
                text: `${Math.round(fraction * 100)}%`,
                style_class: 'hikup-file-meta',
            });
            header.add_child(pct);

            // cancel: kill the hikup process, the server copes with that
            header.add_child(this._actionButton('window-close-symbolic',
                () => this._cancelTransfer(entry)));

            const bar = new HikupProgressBar({
                x_expand: true,
                y_align: Clutter.ActorAlign.CENTER,
                style_class: 'hikup-progress-bar',
            });
            bar.setFraction(fraction);

            box.add_child(header);
            box.add_child(bar);
            item.add_child(box);

            entry._bar = bar;
            entry._pct = pct;

            return item;
        }

        if (entry.state === 'done') {
            const item = new PopupMenu.PopupBaseMenuItem();
            item.add_style_class_name('hikup-upload-item');

            item.add_child(new St.Label({text: '✓', style_class: 'hikup-done-mark'}));
            item.add_child(new St.Label({
                text: entry.name,
                x_expand: true,
                style_class: 'hikup-file-name',
            }));

            item.add_child(this._actionButton('edit-copy-symbolic',
                () => this._copy(`hikup down ${entry.hash} ${entry.server.address}`)));
            if (entry.link)
                item.add_child(this._actionButton('insert-link-symbolic',
                    () => this._copy(entry.link)));

            // downloads open the local file, uploads their HTTP link
            item.connect('activate', () => {
                if (entry.kind === 'download') {
                    try {
                        this._openUri(GLib.filename_to_uri(entry.path, null));
                    } catch (e) {
                        this._copy(entry.path);
                    }
                } else if (entry.link) {
                    this._openUri(entry.link);
                } else if (entry.hash) {
                    this._copy(entry.hash);
                }
            });
            return item;
        }

        // error
        const item = new PopupMenu.PopupBaseMenuItem({reactive: false, activate: false, hover: false});
        item.add_style_class_name('hikup-upload-item');

        item.add_child(new St.Label({text: '✗', style_class: 'hikup-error-item'}));
        item.add_child(new St.Label({
            text: `${entry.name} — ${entry.error}`,
            x_expand: true,
            style_class: 'hikup-error-item',
        }));

        const retry = new St.Button({
            label: 'Retry',
            style_class: 'button hikup-retry-button',
            can_focus: true,
        });
        retry.connect('clicked', () => {
            this._uploads = this._uploads.filter(upload => upload !== entry);
            if (entry.kind === 'download')
                this._startDownload(
                    {name: entry.name, hash: entry.hash, size: entry.total},
                    entry.server);
            else if (entry.kind === 'transfer')
                this._startTransfer(entry.server, entry.url);
            else
                this._startUpload(entry.server, GLib.filename_to_uri(entry.path, null));
            if (this.menu.isOpen)
                this._rebuild();
        });
        item.add_child(retry);
        return item;
    }

    // ── removing ───────────────────────────────────────────────────────────
    // trash button with a two-step confirmation: the first click arms it
    // (turning glass rose), a second click within 4 s runs the removal
    _removeButton(file, server, section) {
        const button = new St.Button({
            can_focus: true,
            style_class: 'hikup-action-button',
            child: new St.Icon({
                icon_name: 'user-trash-symbolic',
                style_class: 'hikup-action-icon',
            }),
        });

        let armed = false;
        let disarmId = 0;

        const disarm = () => {
            armed = false;
            try {
                button.remove_style_class_name('hikup-danger-button');
                button.child.set_icon_name('user-trash-symbolic');
            } catch (e) {
            }
        };

        button.connect('clicked', () => {
            if (!armed) {
                armed = true;
                button.add_style_class_name('hikup-danger-button');
                button.child.set_icon_name('dialog-warning-symbolic');
                disarmId = GLib.timeout_add_seconds(GLib.PRIORITY_DEFAULT, 4, () => {
                    disarm();
                    return GLib.SOURCE_REMOVE;
                });
                return;
            }

            GLib.source_remove(disarmId);
            button.reactive = false;   // no double-fire while running
            this._removeFile(file, server, section);
        });

        return button;
    }

    _removeFile(file, server, section) {
        const hikup = GLib.find_program_in_path('hikup');
        if (!hikup) {
            Main.notify('hikup', 'hikup not found in the PATH of gnome-shell');
            return;
        }

        const argv = [
            hikup,
            server.verify ? 'qvrm' : 'qrm',
            String(file.hash),
            String(server.address ?? ''),
        ];

        let proc;
        try {
            proc = Gio.Subprocess.new(argv,
                Gio.SubprocessFlags.STDOUT_PIPE | Gio.SubprocessFlags.STDERR_PIPE);
        } catch (e) {
            Main.notify('hikup', `could not start hikup: ${e.message}`);
            return;
        }

        proc.communicate_utf8_async(null, null, (p, res) => {
            let error = null;
            try {
                const [, , err] = p.communicate_utf8_finish(res);
                if (p.get_exit_status() !== 0)
                    error = err.trim().split('\n')[0] || `hikup exited with code ${p.get_exit_status()}`;
            } catch (e) {
                error = e.message;
            }

            if (error) {
                Main.notify('hikup', `removing ${file.name} failed: ${error}`);
                return;
            }

            // drop it from the cached listing and refresh the section in place
            const key = serverKey(server);
            const cached = this._cache.get(key);
            if (cached?.state === 'done')
                cached.files = cached.files.filter(f => f.hash !== file.hash);

            if (this.menu.isOpen) {
                const liveSection = this._sections.find(s => s.server === server);
                if (liveSection && cached?.state === 'done') {
                    this._renderFiles(liveSection, cached.files);
                    this._applyFilter();
                } else {
                    this._rebuild();
                }
            }
        });
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

    // ── panel progress bar ────────────────────────────────────────────────
    // aggregate fraction of all running transfers, shown under the icon;
    // the bar actor is added/removed with running transfers (it overlays
    // the button, so the icon never moves)
    _updatePanelBar() {
        try {
            const running = this._uploads.filter(upload => upload.state === 'running');

            if ( running.length === 0 ) {
                if ( this._panelBar.get_parent() !== null )
                    this._panelBox.remove_child(this._panelBar);
                return;
            }

            let sent = 0;
            let total = 0;
            for ( const upload of running )
                if ( upload.total > 0 ) {
                    sent += upload.sent;
                    total += upload.total;
                }

            if ( this._panelBar.get_parent() === null )
                this._panelBox.add_child(this._panelBar);
            this._panelBar.setFraction(total > 0 ? Math.min(1, sent / total) : 0);
        } catch (e) {
            // the button was destroyed (extension disabled)
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

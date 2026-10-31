import Adw from 'gi://Adw';
import Gtk from 'gi://Gtk';
import GLib from 'gi://GLib';

const SERVERS_FILE = GLib.build_filenamev([GLib.get_user_config_dir(), 'hikup', 'servers.json']);

function loadServers() {
    try {
        const [ok, contents] = GLib.file_get_contents(SERVERS_FILE);
        if (!ok)
            return [];
        const servers = JSON.parse(new TextDecoder().decode(contents));
        return Array.isArray(servers) ? servers : [];
    } catch (e) {
        return [];
    }
}

function saveServers(servers) {
    GLib.mkdir_with_parents(GLib.path_get_dirname(SERVERS_FILE), 0o755);
    GLib.file_set_contents(SERVERS_FILE, JSON.stringify(servers, null, 2));
}

const FIELDS = [
    ['name', 'Name', false],
    ['address', 'Address', false],
    ['http', 'HTTP URL (optional)', false],
    ['user', 'User', false],
    ['pass', 'Password', true],
];

export default class HikupPreferences {
    fillPreferencesWindow(window) {
        const page = new Adw.PreferencesPage({
            title: 'Servers',
            icon_name: 'folder-download-symbolic',
        });
        window.add(page);

        const group = new Adw.PreferencesGroup({
            title: 'Hikup servers',
            description: `Stored in ${SERVERS_FILE} (plain text — keep that in mind)`,
        });
        page.add(group);

        const servers = loadServers();
        const list = new Gtk.Box({orientation: Gtk.Orientation.VERTICAL, spacing: 12});
        group.add(list);

        const save = () => saveServers(servers);

        const addServer = server => {
            const remove = () => {
                list.remove(row);
                const index = servers.indexOf(server);
                if (index >= 0)
                    servers.splice(index, 1);
                save();
            };

            const row = new Gtk.Box({orientation: Gtk.Orientation.VERTICAL, spacing: 4});

            for (const [key, label, isPassword] of FIELDS) {
                const line = new Gtk.Box({spacing: 8});
                line.append(new Gtk.Label({label: `${label}:`, xalign: 0, width_request: 120}));

                const entry = new Gtk.Entry({
                    hexpand: true,
                    text: server[key] ?? '',
                    visibility: !isPassword,
                    input_purpose: isPassword
                        ? Gtk.InputPurpose.PASSWORD
                        : Gtk.InputPurpose.FREE_FORM,
                });
                entry.connect('changed', () => {
                    server[key] = entry.text;
                    save();
                });
                line.append(entry);

                if (key === 'name') {
                    const button = Gtk.Button.new_from_icon_name('user-trash-symbolic');
                    button.set_valign(Gtk.Align.CENTER);
                    button.connect('clicked', remove);
                    line.append(button);
                }

                row.append(line);
            }

            // verify TLS toggle — selects vup instead of up for uploads
            const verifyRow = new Gtk.Box({spacing: 8});
            verifyRow.append(new Gtk.Label({
                label: 'Verify TLS (vup):',
                xalign: 0,
                width_request: 120,
            }));
            const check = new Gtk.CheckButton({active: !!server.verify});
            check.connect('toggled', () => {
                server.verify = check.active;
                save();
            });
            verifyRow.append(check);
            row.append(verifyRow);

            list.append(row);
        };

        for (const server of servers)
            addServer(server);

        const add = new Gtk.Button({label: 'Add server', halign: Gtk.Align.CENTER});
        add.connect('clicked', () => {
            const server = {name: '', address: '', http: '', user: '', pass: ''};
            servers.push(server);
            addServer(server);
            save();
        });
        group.add(add);
    }
}

// Tidy -- the Quick Settings tile (s012c on the road).
//
// Scott: "a tile ... once there are N files needing attention we get a
// notification. the tile can kick off a scan or create a project of X type
// to its appropriate folder" -- "an expanded tile with a few features and
// dropdowns".
//
// The tile READS ONLY: Tidy's attention.ini (written by `tidy --scan`),
// settings.ini, the saved templates, whether the timer unit is there. It
// never writes a file. Everything that writes goes through Tidy itself, which
// shows its preview first: `tidy --new-project <template>` opens New project
// with that template picked; `tidy --background` opens the Background scan
// window. A running Tidy receives these (GApplication) -- no second window.
//
// The tile is lit (checked) when what needs attention has reached "Notify at";
// a small Tidy icon shows in the top bar then too. Clicking the tile opens its
// menu.

import GObject from 'gi://GObject';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';

import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as PopupMenu from 'resource:///org/gnome/shell/ui/popupMenu.js';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import {QuickMenuToggle, SystemIndicator} from 'resource:///org/gnome/shell/ui/quickSettings.js';

const DATA = GLib.build_filenamev([GLib.get_user_data_dir(), 'tidy']);
const UNITS = GLib.build_filenamev([GLib.get_user_config_dir(), 'systemd', 'user']);

function readText(path) {
    try {
        const [ok, bytes] = GLib.file_get_contents(path);
        return ok ? new TextDecoder().decode(bytes) : '';
    } catch (e) {
        return '';
    }
}

// "key=value" lines -> {key: value}; comments and junk skipped.
function readIni(path) {
    const out = {};
    for (const line of readText(path).split('\n')) {
        if (!line || line.startsWith('#')) continue;
        const eq = line.indexOf('=');
        if (eq > 0) out[line.slice(0, eq).trim()] = line.slice(eq + 1).trim();
    }
    return out;
}

function num(v, fallback = 0) {
    const n = Number.parseInt(v ?? '', 10);
    return Number.isFinite(n) && n >= 0 ? n : fallback;
}

// The same words as core::due::line().
function attentionLine(a) {
    const total = num(a.remove) + num(a.archive) + num(a.settle) + num(a.closet) + num(a.lists);
    if (!total) return [0, 'nothing needs attention'];
    const parts = [];
    if (num(a.remove)) parts.push(`${num(a.remove)} to remove`);
    if (num(a.archive)) parts.push(`${num(a.archive)} to archive`);
    if (num(a.settle)) parts.push(`${num(a.settle)} to settle`);
    if (num(a.closet)) parts.push(`${num(a.closet)} in the Closet`);
    if (num(a.lists)) parts.push(`${num(a.lists)} id list${num(a.lists) === 1 ? '' : 's'}`);
    return [total, parts.join(', ')];
}

// The installed tidy: ~/.local (install.sh's default), /usr/local, then PATH
// (the Shell's PATH often lacks ~/.local/bin).
function tidyBinary() {
    for (const p of [GLib.build_filenamev([GLib.get_home_dir(), '.local', 'bin', 'tidy']), '/usr/local/bin/tidy'])
        if (GLib.file_test(p, GLib.FileTest.IS_EXECUTABLE)) return p;
    return GLib.find_program_in_path('tidy');
}

// Saved templates: <data>/templates/<name>.template -> names, sorted.
function templateNames() {
    const names = [];
    try {
        const dir = Gio.File.new_for_path(GLib.build_filenamev([DATA, 'templates']));
        const en = dir.enumerate_children('standard::name,standard::type', Gio.FileQueryInfoFlags.NOFOLLOW_SYMLINKS, null);
        for (let info; (info = en.next_file(null));) {
            const n = info.get_name();
            if (info.get_file_type() === Gio.FileType.REGULAR && n.endsWith('.template') && !n.startsWith('.'))
                names.push(n.slice(0, -'.template'.length));
        }
        en.close(null);
    } catch (e) {
        // none saved yet
    }
    return names.sort();
}

const TidyToggle = GObject.registerClass(
class TidyToggle extends QuickMenuToggle {
    _init(ext) {
        const gicon = Gio.icon_new_for_string(GLib.build_filenamev([ext.path, 'icons', 'tidy-symbolic.svg']));
        super._init({title: 'Tidy', subtitle: '', gicon, toggleMode: false});
        this._gicon = gicon;
        // The tile itself opens the menu ("an expanded tile").
        this.connect('clicked', () => this.menu.open());

        this.menu.setHeader(gicon, 'Tidy', '');

        this._scan = new PopupMenu.PopupMenuItem('Scan now');
        this._scan.connect('activate', () => this._scanNow());
        this.menu.addMenuItem(this._scan);

        this._newProject = new PopupMenu.PopupSubMenuMenuItem('New project');
        this.menu.addMenuItem(this._newProject);

        this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
        for (const [label, args] of [['Due…', ['--page', 'due']],
                                     ['The Closet…', ['--page', 'closet']],
                                     ['Projects…', ['--page', 'projects']]]) {
            const item = new PopupMenu.PopupMenuItem(label);
            item.connect('activate', () => this._tidy(args));
            this.menu.addMenuItem(item);
        }
        this._bg = new PopupMenu.PopupMenuItem('Background scan…');
        this._bg.connect('activate', () => this._tidy(['--background']));
        this.menu.addMenuItem(this._bg);

        this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
        const open = new PopupMenu.PopupMenuItem('Open Tidy');
        open.connect('activate', () => this._tidy([]));
        this.menu.addMenuItem(open);

        // The menu is rebuilt from the files each time it opens -- cheap, and never stale.
        this.menu.connect('open-state-changed', (m, isOpen) => {
            if (isOpen) this._fillTemplates();
        });
    }

    // attention.ini + settings.ini + the timer unit -> the subtitle, lit, header.
    refresh() {
        const a = readIni(GLib.build_filenamev([DATA, 'attention.ini']));
        const s = readIni(GLib.build_filenamev([DATA, 'settings.ini']));
        const notifyAt = Math.max(num(s.notify_at, 10), 1);
        const [total, parts] = attentionLine(a);
        const looked = num(a.when);
        const timer = readText(GLib.build_filenamev([UNITS, 'tidy-scan.timer']));
        const minutes = /\nOnUnitActiveSec=(\d+)min/.exec(timer)?.[1];
        const bgOn = timer.startsWith('# Tidy background scan') && minutes;

        this.subtitle = !looked ? 'no look yet' : total ? `${total} need${total === 1 ? 's' : ''} attention` : 'all tidy';
        this.checked = total >= notifyAt;
        let when = 'never';
        if (looked) when = GLib.DateTime.new_from_unix_local(looked).format('%a %H:%M');
        this.menu.setHeader(this._gicon, 'Tidy', looked ? `${parts} — looked ${when}` : 'No look yet — Scan now');
        this._bg.label.text = bgOn ? `Background scan: every ${minutes} min…` : 'Background scan: off…';
        return this.checked;
    }

    _fillTemplates() {
        this._newProject.menu.removeAll();
        const names = templateNames();
        if (!names.length) {
            const none = new PopupMenu.PopupMenuItem('No templates yet — Templates in Tidy');
            none.connect('activate', () => this._tidy(['--page', 'templates']));
            this._newProject.menu.addMenuItem(none);
            return;
        }
        for (const n of names) {
            const item = new PopupMenu.PopupMenuItem(n);
            item.connect('activate', () => this._tidy(['--new-project', n]));
            this._newProject.menu.addMenuItem(item);
        }
    }

    // Opens Tidy (or tells the running one). Its own windows preview anything written.
    _tidy(args) {
        const bin = tidyBinary();
        if (!bin) {
            Main.notify('Tidy', 'tidy is not installed where the tile looks (~/.local/bin, /usr/local/bin, PATH)');
            return;
        }
        Main.panel.closeQuickSettings?.();
        try {
            Gio.Subprocess.new([bin, ...args], Gio.SubprocessFlags.NONE);
        } catch (e) {
            logError(e, 'Tidy tile');
        }
    }

    // `tidy --scan`: read-only look; attention.ini changes -> the monitor refreshes us.
    _scanNow() {
        const bin = tidyBinary();
        if (!bin) {
            Main.notify('Tidy', 'tidy is not installed where the tile looks (~/.local/bin, /usr/local/bin, PATH)');
            return;
        }
        this.subtitle = 'looking…';
        try {
            const p = Gio.Subprocess.new([bin, '--scan'], Gio.SubprocessFlags.STDOUT_SILENCE | Gio.SubprocessFlags.STDERR_SILENCE);
            p.wait_async(null, () => this.refresh());
        } catch (e) {
            logError(e, 'Tidy tile');
            this.refresh();
        }
    }
});

const TidyIndicator = GObject.registerClass(
class TidyIndicator extends SystemIndicator {
    _init(ext) {
        super._init();
        this._toggle = new TidyToggle(ext);
        this.quickSettingsItems.push(this._toggle);

        // The top-bar icon: shown only while something needs attention past N.
        this._indicator = this._addIndicator();
        this._indicator.gicon = this._toggle._gicon;
        this._update();

        // Tidy writes its files by temp + rename: watch the folder, not the file.
        this._monitor = null;
        try {
            const dir = Gio.File.new_for_path(DATA);
            this._monitor = dir.monitor_directory(Gio.FileMonitorFlags.WATCH_MOVES, null);
            this._monitorId = this._monitor.connect('changed', (m, file, other) => {
                const names = [file?.get_basename(), other?.get_basename()];
                if (names.some(n => n === 'attention.ini' || n === 'settings.ini')) this._update();
            });
        } catch (e) {
            logError(e, 'Tidy tile: no folder watch');
        }
        // The timer unit lives elsewhere; a slow poll catches it being turned on / off.
        this._poll = GLib.timeout_add_seconds(GLib.PRIORITY_LOW, 60, () => {
            this._update();
            return GLib.SOURCE_CONTINUE;
        });
    }

    _update() {
        this._indicator.visible = this._toggle.refresh();
    }

    destroy() {
        if (this._poll) GLib.source_remove(this._poll);
        this._poll = 0;
        if (this._monitor) {
            this._monitor.disconnect(this._monitorId);
            this._monitor.cancel();
            this._monitor = null;
        }
        this.quickSettingsItems.forEach(item => item.destroy());
        super.destroy();
    }
});

export default class TidyExtension extends Extension {
    enable() {
        this._indicator = new TidyIndicator(this);
        Main.panel.statusArea.quickSettings.addExternalIndicator(this._indicator);
    }

    disable() {
        this._indicator?.destroy();
        this._indicator = null;
    }
}

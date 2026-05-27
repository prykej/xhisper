#!/usr/bin/python3
# NOTE: Must use system Python — gi.repository is not available in conda/venv.
#   The install script sets the desktop entry to /usr/bin/python3 explicitly.
"""
xhisper-indicator — System tray indicator for xhisper dictation.

Requires: gir1.2-ayatanaappindicator3-0.1, python3-gi, python3-gi-cairo
Build-dep: libayatana-appindicator3-1

Run:  xhisper-indicator
Or enable autostart via systemd or desktop entry.
"""

import gi
gi.require_version('Gtk', '3.0')
gi.require_version('AyatanaAppIndicator3', '0.1')
from gi.repository import Gtk, GLib, AyatanaAppIndicator3

import subprocess
import os
import signal
import threading
import time

# ── Paths ──────────────────────────────────────────────────────────────────

BIN_XHISPER    = "/usr/local/bin/xhisper"
BIN_XHISPER_KEYD = "/usr/local/bin/xhisper-keyd"
LOG_FILE       = "/tmp/xhisper.log"
PIDFILE_KEYD   = os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), "xhisper-keyd.pid")

# ── Icon names (use system theme icons, no custom files needed) ────────────

ICON_IDLE        = "edit-paste-symbolic"
ICON_RECORDING   = "media-record-symbolic"
ICON_TRANSCRIBING = "view-refresh-symbolic"

# ── State ──────────────────────────────────────────────────────────────────

class XhisperIndicator:
    def __init__(self):
        self.recording = False
        self.monitor_id = None

        # Build indicator
        self.indicator = AyatanaAppIndicator3.Indicator.new(
            "xhisper-indicator",
            ICON_IDLE,
            AyatanaAppIndicator3.IndicatorCategory.HARDWARE
        )
        self.indicator.set_status(AyatanaAppIndicator3.IndicatorStatus.ACTIVE)
        self.indicator.set_title("xhisper — Dictation")
        self.indicator.set_label("", "")  # no extra label text

        # Build menu
        self.menu = Gtk.Menu()

        self.item_toggle = Gtk.MenuItem(label="⏺  Start Recording")
        self.item_toggle.connect("activate", self.on_toggle)
        self.menu.append(self.item_toggle)

        self.item_transcribe = Gtk.MenuItem(label="⏹  Stop & Transcribe")
        self.item_transcribe.connect("activate", self.on_transcribe)
        self.item_transcribe.set_sensitive(False)
        self.menu.append(self.item_transcribe)

        sep = Gtk.SeparatorMenuItem()
        self.menu.append(sep)

        item_logs = Gtk.MenuItem(label="📋  View Logs")
        item_logs.connect("activate", self.on_view_logs)
        self.menu.append(item_logs)

        item_clear = Gtk.MenuItem(label="🗑  Clear Logs")
        item_clear.connect("activate", self.on_clear_logs)
        self.menu.append(item_clear)

        sep2 = Gtk.SeparatorMenuItem()
        self.menu.append(sep2)

        item_keyd = Gtk.MenuItem(label="🔄  Restart Hotkey Daemon")
        item_keyd.connect("activate", self.on_restart_keyd)
        self.menu.append(item_keyd)

        item_quit = Gtk.MenuItem(label="✕  Quit")
        item_quit.connect("activate", self.on_quit)
        self.menu.append(item_quit)

        self.menu.show_all()
        self.indicator.set_menu(self.menu)

        # Left-click = toggle
        self.indicator.connect("scroll-event", self.on_scroll)

        # Ensure keyd is running
        self.ensure_keyd()

        # Start monitoring recording state
        self.monitor_id = GLib.timeout_add(500, self.check_recording_state)

    # ── Helpers ─────────────────────────────────────────────────────────────

    def run_xhisper(self):
        """Run xhisper in background, return immediately."""
        env = os.environ.copy()
        env["DISPLAY"] = os.environ.get("DISPLAY", ":0")
        # Load GROQ_API_KEY from ~/.env
        home = os.environ.get("HOME", "")
        env_file = os.path.join(home, ".env")
        if os.path.isfile(env_file):
            with open(env_file) as f:
                for line in f:
                    line = line.strip()
                    if line.startswith("GROQ_API_KEY="):
                        env["GROQ_API_KEY"] = line.split("=", 1)[1]
                        break
        subprocess.Popen(
            [BIN_XHISPER],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            env=env,
            start_new_session=True
        )

    def is_keyd_running(self):
        """Check if xhisper-keyd is running."""
        if os.path.isfile(PIDFILE_KEYD):
            try:
                with open(PIDFILE_KEYD) as f:
                    pid = int(f.read().strip())
                os.kill(pid, 0)  # signal 0 = check existence
                return True
            except (ValueError, ProcessLookupError, PermissionError):
                return False
        return False

    def ensure_keyd(self):
        """Start xhisper-keyd if not already running."""
        if not self.is_keyd_running():
            subprocess.Popen(
                [BIN_XHISPER_KEYD],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                start_new_session=True
            )
            time.sleep(1)

    def is_recording(self):
        """Check if pw-record is currently recording."""
        try:
            result = subprocess.run(
                ["pgrep", "-f", "pw-record.*/tmp/xhisper.wav"],
                capture_output=True, timeout=2
            )
            return result.returncode == 0
        except Exception:
            return False

    def check_recording_state(self):
        """Poll recording state and update icon/menu."""
        rec = self.is_recording()
        if rec != self.recording:
            self.recording = rec
            if rec:
                self.indicator.set_icon_full(ICON_RECORDING, "Recording...")
                self.item_toggle.set_label("⏺  Recording...")
                self.item_toggle.set_sensitive(False)
                self.item_transcribe.set_sensitive(True)
            else:
                self.indicator.set_icon_full(ICON_IDLE, "xhisper — Dictation")
                self.item_toggle.set_label("⏺  Start Recording")
                self.item_toggle.set_sensitive(True)
                self.item_transcribe.set_sensitive(False)
        return True  # keep timeout active

    # ── Callbacks ───────────────────────────────────────────────────────────

    def on_toggle(self, widget):
        """Left-click / menu: toggle recording."""
        if not self.recording:
            self.run_xhisper()

    def on_transcribe(self, widget):
        """Stop recording and transcribe."""
        if self.recording:
            self.run_xhisper()  # second run = stop + transcribe

    def on_view_logs(self, widget):
        """Show log file in a popup dialog."""
        text = ""
        if os.path.isfile(LOG_FILE):
            with open(LOG_FILE) as f:
                # Show last 40 lines
                lines = f.readlines()
                text = "".join(lines[-40:])
        else:
            text = "(no log file found)"

        dialog = Gtk.MessageDialog(
            flags=Gtk.DialogFlags.MODAL,
            type=Gtk.MessageType.INFO,
            buttons=Gtk.ButtonsType.CLOSE,
            message_format="xhisper logs"
        )
        dialog.format_secondary_text(text[-2000:])  # cap length
        dialog.set_default_size(600, 400)
        dialog.run()
        dialog.destroy()

    def on_clear_logs(self, widget):
        """Clear the log file."""
        try:
            os.remove(LOG_FILE)
        except FileNotFoundError:
            pass

    def on_restart_keyd(self, widget):
        """Kill and restart xhisper-keyd."""
        # Kill existing
        if os.path.isfile(PIDFILE_KEYD):
            try:
                with open(PIDFILE_KEYD) as f:
                    pid = int(f.read().strip())
                os.kill(pid, signal.SIGTERM)
                time.sleep(0.5)
            except Exception:
                pass
        # Start new
        self.ensure_keyd()

    def on_scroll(self, indicator, steps, direction):
        """Scroll on icon = toggle."""
        self.on_toggle(None)

    def on_quit(self, widget):
        """Quit indicator (but leave keyd running)."""
        if self.monitor_id:
            GLib.source_remove(self.monitor_id)
        Gtk.main_quit()


# ── Main ────────────────────────────────────────────────────────────────────

def main():
    # Allow only one instance
    lock_file = os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), "xhisper-indicator.lock")
    import fcntl
    try:
        lf = open(lock_file, "w")
        fcntl.flock(lf, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except IOError:
        print("xhisper-indicator is already running.")
        return

    # Handle SIGINT/SIGTERM
    signal.signal(signal.SIGINT, lambda *a: Gtk.main_quit())
    signal.signal(signal.SIGTERM, lambda *a: Gtk.main_quit())

    indicator = XhisperIndicator()
    Gtk.main()

    # Cleanup lock
    try:
        fcntl.flock(lf, fcntl.LOCK_UN)
        lf.close()
        os.unlink(lock_file)
    except Exception:
        pass


if __name__ == "__main__":
    main()

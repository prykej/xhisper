<div align="center">
  <h1>xhisper <i>/ˈzɪspər/</i></h1>
  <img src="demo.gif" alt="xhisper demo" width="300">
  <br><br>
</div>

Dictation at cursor for Linux.

## Quick Install (Ubuntu 22.04+)

```bash
git clone https://github.com/prykej/xhisper.git
cd xhisper && chmod +x install.sh && ./install.sh
```

**Prerequisite:** Groq API key from [console.groq.com](https://console.groq.com) (free). If you don't have `~/.env` set up, the installer will prompt you.

After install, enable the hotkey daemon:

```bash
systemctl --user enable --now xhisper-keyd
```

**Hotkey: `LeftCtrl + Super + LeftAlt`** — press and release to toggle recording.

Log out and back in for the `input` group change the installer makes (or if `groups` doesn't show `input` yet).

---

## Manual Setup

### Dependencies

<details>
<summary>Arch Linux / Manjaro</summary>
<pre><code>sudo pacman -S pipewire jq curl ffmpeg gcc</code></pre>
</details>

<details>
<summary>Debian / Ubuntu / Linux Mint</summary>
<pre><code>sudo apt update
sudo apt install pipewire jq curl ffmpeg gcc</code></pre>
</details>

<details>
<summary>Fedora / RHEL / AlmaLinux / Rocky</summary>
<pre><code>sudo dnf install -y pipewire pipewire-utils jq curl ffmpeg gcc</code></pre>
</details>

<details>
<summary>OpenSUSE (Leap / Tumbleweed)</summary>
<pre><code>sudo zypper refresh
sudo zypper install pipewire jq curl ffmpeg gcc</code></pre>
</details>

<details>
<summary>Void Linux</summary>
<pre><code>sudo xbps-install -S
sudo xbps-install pipewire jq curl ffmpeg gcc</code></pre>
</details>

**Note:** `wl-clipboard` (Wayland) or `xclip` (X11) required for non-ASCII but usually pre-installed.

### Build & Install

```bash
git clone --depth 1 https://github.com/prykej/xhisper.git
cd xhisper && make
sudo make install
```

### Hotkey Setup (xhisper-keyd)

The hotkey daemon (`xhisper-keyd`) listens for `LeftCtrl + Super + LeftAlt` and toggles xhisper recording globally.

```bash
# Add user to input group if not already
sudo usermod -aG input $USER
# Log out and back in

# Start manually
xhisper-keyd -v

# Or enable as a systemd user service
systemctl --user enable --now xhisper-keyd
```

Alternative: keep using xhisper via your WM keybinding (see below) — run it twice to toggle.

### WM / Desktop Keybinding

You can still bind `xhisper` directly to any key in your WM if you prefer not to use the hotkey daemon.

<details>
<summary>keyd</summary>

```ini
[main]
capslock = layer(dictate)

[dictate:C]
d = macro(xhisper)
```
</details>

<details>
<summary>sxhkd</summary>

```
super + d
    xhisper
```
</details>

<details>
<summary>i3 / sway</summary>

```
bindsym $mod+d exec xhisper
```
</details>

<details>
<summary>Hyprland</summary>

```
bind = $mainMod, D, exec, xhisper
```
</details>

<details>
<summary>Gnome</summary>

```sh
# In your terminal:

name="xhisper"
binding="<CTRL><SHIFT>X"
action="/usr/local/bin/xhisper"

media_keys=org.gnome.settings-daemon.plugins.media-keys
custom_kbd=org.gnome.settings-daemon.plugins.media-keys.custom-keybinding
kbd_path=/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/$name/
new_bindings=`gsettings get $media_keys custom-keybindings | sed -e"s>'\\]>','$kbd_path']>"| sed -e"s>@as \\[\\]>['$kbd_path']>"`
gsettings set $media_keys custom-keybindings "$new_bindings"
gsettings set $custom_kbd:$kbd_path name "$name"
gsettings set $custom_kbd:$kbd_path binding "$binding"
gsettings set $custom_kbd:$kbd_path command "$action"
```
</details>

---

## Usage

### With hotkey daemon (recommended)

Press and release `LeftCtrl + Super + LeftAlt`:
- **First press**: Starts recording
- **Second press**: Stops and transcribes

The transcription is typed at your cursor position.

### Without hotkey daemon (manual toggle)

Simply run `xhisper` twice via your WM keybinding:
- **First run**: Starts recording
- **Second run**: Stops and transcribes

**View logs:**
```sh
xhisper --log
```

**Non-QWERTY layouts:**

For non-QWERTY layouts (e.g. Dvorak, International), set up an input switch key to QWERTY (e.g. rightalt). Then instead of binding to `xhisper`, bind to:
```sh
xhisper --<your-input-switch-key>
```

**Available input switch keys:** `--leftalt`, `--rightalt`, `--leftctrl`, `--rightctrl`, `--leftshift`, `--rightshift`, `--super`

Key chords (like ctrl-space) not available yet.

---

## Configuration

Configuration is read from `~/.config/xhisper/xhisperrc`:

```sh
mkdir -p ~/.config/xhisper
cp default_xhisperrc ~/.config/xhisper/xhisperrc
```

---

## Troubleshooting

**Terminal Applications**: Clipboard paste uses Ctrl+V, which doesn't work in terminal emulators (they require Ctrl+Shift+V). Temporary workaround is to remap Ctrl+V to paste in your terminal emulator's settings. Note that *this limitation only affects international/Unicode characters*. ASCII characters (a-z, A-Z, 0-9, punctuation) are typed directly and doesn't care whether terminal or not.

**Non-ASCII Transcription**: Increase non-ascii-*-delay to give the transcription longer timing buffer.

**Permission denied on /dev/input/event***: Make sure you're in the `input` group and have logged out and back in:
```sh
sudo usermod -aG input $USER
# Log out and back in, then check:
groups  # should include 'input'
```

**xhisper-keyd not finding keyboards**: Run with `-v` flag to see which devices are monitored:
```sh
xhisper-keyd -v
```

---

<p align="center">
  <em>Low complexity dictation for Linux</em>
</p>

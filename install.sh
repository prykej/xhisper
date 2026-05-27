#!/bin/bash
#
# xhisper — One-command installer for Ubuntu 22.04+
# Usage: curl -fsSL https://raw.githubusercontent.com/prykej/xhisper/main/install.sh | bash
#    Or: ./install.sh (after cloning)

set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="/usr/local"
BINDIR="$PREFIX/bin"
SYSTEMD_DIR="$HOME/.config/systemd/user"
AUTOSTART_DIR="$HOME/.config/autostart"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}[✓]${NC} $*"; }
warn()  { echo -e "${YELLOW}[!]${NC} $*"; }
error() { echo -e "${RED}[✗]${NC} $*" >&2; }

# ── Pre-flight ──────────────────────────────────────────────────────────

if [ "$(id -u)" -eq 0 ]; then
    error "Do NOT run this script as root/sudo. It will use sudo internally when needed."
    exit 1
fi

# ── Install dependencies ─────────────────────────────────────────────────

echo ""
echo "═══ xhisper installer for Ubuntu 24.04+ ═══"
echo ""

MISSING_PKGS=""
for pkg in pipewire jq curl ffmpeg gcc xdg-utils python3-gi python3-gi-cairo; do
    if ! dpkg -s "$pkg" &>/dev/null 2>&1; then
        MISSING_PKGS="$MISSING_PKGS $pkg"
    fi
done

if [ -n "$MISSING_PKGS" ]; then
    echo "Installing dependencies:$MISSING_PKGS"
    sudo apt-get update -qq
    sudo apt-get install -y -qq $MISSING_PKGS
    info "Dependencies installed."
else
    info "All dependencies already installed."
fi

# Clipboard: wl-clipboard (Wayland) or xclip (X11)
if [ "$XDG_SESSION_TYPE" = "wayland" ]; then
    if ! command -v wl-copy &>/dev/null; then
        sudo apt-get install -y -qq wl-clipboard
        info "Installed wl-clipboard (Wayland clipboard)."
    fi
elif ! command -v xclip &>/dev/null; then
    sudo apt-get install -y -qq xclip
    info "Installed xclip (X11 clipboard)."
fi

# ── Add user to input group ──────────────────────────────────────────────

if ! groups | grep -qw input; then
    echo "Adding user to 'input' group (needed for /dev/uinput and evdev)..."
    sudo usermod -aG input "$USER"
    warn "You've been added to the 'input' group."
    warn "You MUST log out and log back in (or reboot) for this to take effect."
    warn "After logging back in, re-run this script to complete installation."
    exit 0
else
    info "User is in 'input' group."
fi

# ── Install udev rule for /dev/uinput ─────────────────────────────────────

UDEV_RULE="/etc/udev/rules.d/99-xhisper-uinput.rules"
if [ ! -f "$UDEV_RULE" ]; then
    echo "Installing udev rule for /dev/uinput access..."
    sudo tee "$UDEV_RULE" > /dev/null <<'RULE'
# xhisper — allow input group to access /dev/uinput for virtual keyboard input
KERNEL=="uinput", GROUP="input", MODE="0660"
RULE
    sudo udevadm control --reload-rules
    sudo udevadm trigger --name-match=uinput
    info "udev rule installed. /dev/uinput is now accessible by 'input' group."
else
    info "udev rule already present."
fi

# ── Build ────────────────────────────────────────────────────────────────

echo ""
echo "Building xhisper..."
cd "$REPO_DIR"
make clean 2>/dev/null || true
make
info "Build successful."

# ── Install binaries ─────────────────────────────────────────────────────

echo ""
echo "Installing to $BINDIR..."
sudo install -m 755 xhispertool "$BINDIR/xhispertool"
sudo ln -sf xhispertool "$BINDIR/xhispertoold"
sudo install -m 755 xhisper.sh "$BINDIR/xhisper"
sudo install -m 755 xhisper-keyd "$BINDIR/xhisper-keyd"
info "Binaries installed."

# ── Install config ───────────────────────────────────────────────────────

CONFIG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/xhisper"
mkdir -p "$CONFIG_DIR"
if [ ! -f "$CONFIG_DIR/xhisperrc" ]; then
    cp "$REPO_DIR/default_xhisperrc" "$CONFIG_DIR/xhisperrc"
    info "Default config installed to $CONFIG_DIR/xhisperrc"
else
    info "Config already exists at $CONFIG_DIR/xhisperrc (not overwritten)"
fi

# ── Setup GROQ_API_KEY ───────────────────────────────────────────────────

echo ""
if [ -f "$HOME/.env" ] && grep -q "GROQ_API_KEY" "$HOME/.env"; then
    info "GROQ_API_KEY found in ~/.env"
else
    echo -e "${YELLOW}⚠ GROQ_API_KEY not found in ~/.env${NC}"
    echo ""
    echo "xhisper needs a Groq API key for transcription (free tier available)."
    echo "Get one at: https://console.groq.com"
    echo ""
    read -rp "Enter your Groq API key (or press Enter to skip): " key
    if [ -n "$key" ]; then
        if [ -f "$HOME/.env" ]; then
            # Remove old key if present
            sed -i '/^GROQ_API_KEY=/d' "$HOME/.env"
        fi
        echo "GROQ_API_KEY=$key" >> "$HOME/.env"
        info "API key saved to ~/.env"
    else
        warn "Skipped. Add GROQ_API_KEY=<your_key> to ~/.env later."
    fi
fi

# ── Install systemd user service for xhisper-keyd ────────────────────────

echo ""
echo "Setting up xhisper-keyd (hotkey daemon)..."

mkdir -p "$SYSTEMD_DIR"
cat > "$SYSTEMD_DIR/xhisper-keyd.service" <<EOF
[Unit]
Description=xhisper hotkey daemon (Ctrl+Alt+Super to toggle dictation)
After=graphical-session.target
PartOf=graphical-session.target

[Service]
Type=simple
ExecStart=$BINDIR/xhisper-keyd -v
Restart=on-failure
RestartSec=3
Environment=DISPLAY=:0

[Install]
WantedBy=graphical-session.target
EOF

systemctl --user daemon-reload 2>/dev/null || true
info "systemd user service installed."

# ── Also install a .desktop autostart as fallback ────────────────────────

mkdir -p "$AUTOSTART_DIR"
cat > "$AUTOSTART_DIR/xhisper-keyd.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=xhisper-keyd
Comment=Hotkey daemon for xhisper dictation
Exec=$BINDIR/xhisper-keyd
Hidden=false
X-GNOME-Autostart-enabled=true
EOF
info "Desktop autostart entry installed (fallback)."

# ── Install xhisper-indicator (system tray) ────────────────────────────────

echo ""
echo "Setting up xhisper-indicator (system tray)..."

cat > "$AUTOSTART_DIR/xhisper-indicator.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=xhisper-indicator
Comment=xhisper dictation system tray indicator
Exec=/usr/bin/python3 $BINDIR/xhisper-indicator
Hidden=false
X-GNOME-Autostart-enabled=true
EOF
info "Indicator autostart entry installed."

# ── Summary ──────────────────────────────────────────────────────────────

echo ""
echo "═══════════════════════════════════════════════════"
echo "  xhisper installed successfully!"
echo "═══════════════════════════════════════════════════"
echo ""
echo "  Hotkey:  LeftCtrl + Super + LeftAlt"
echo "  (press and release to start/stop recording)"
echo ""
echo "  Enable auto-start at login:"
echo "    systemctl --user enable --now xhisper-keyd"
echo ""
echo "  Manual start:"
echo "    xhisper-keyd -v"
echo ""
echo "  Toggle dictation manually without hotkey:"
echo "    xhisper"
echo ""
echo "  View logs:"
echo "    xhisper --log"
echo ""
echo "  System tray indicator: xhisper-indicator"
echo "    (auto-starts at login, or run manually)"
echo ""
echo "  Config: ~/.config/xhisper/xhisperrc"
echo ""

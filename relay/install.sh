#!/bin/sh
# install.sh -- put the relay in place on its LXC (docs/RELAY.md, R3).
# Run with sudo, from your own login, on the unpacked twoegram-relay folder;
# nothing else needs root, and the relay never runs as root. Safe to run again to
# update: the gateways and Apples it knows are left alone.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
command -v python3 >/dev/null || { echo "install.sh: needs python3 (apt install python3)" >&2; exit 1; }
python3 -c 'import sys; sys.exit(sys.version_info < (3, 11))' ||
    { echo "install.sh: needs Python 3.11 or later" >&2; exit 1; }

id twoegram-relay >/dev/null 2>&1 ||
    useradd --system --home-dir /var/lib/twoegram-relay --shell /usr/sbin/nologin twoegram-relay

install -d -o root -g root -m 0755 /opt/twoegram-relay
rm -rf /opt/twoegram-relay/twoegram /opt/twoegram-relay/twoegram_relay
cp -R "$here/twoegram" "$here/twoegram_relay" /opt/twoegram-relay/
install -o root -g root -m 0755 "$here/twoegram-relay" /opt/twoegram-relay/twoegram-relay
chown -R root:root /opt/twoegram-relay
chmod -R go-w /opt/twoegram-relay

# Its own files: only it may read them.
install -d -o twoegram-relay -g twoegram-relay -m 0700 /etc/twoegram-relay /var/lib/twoegram-relay

install -o root -g root -m 0644 "$here/twoegram-relay.service" /etc/systemd/system/twoegram-relay.service
systemctl daemon-reload

cat <<'MSG'
Installed. Run the commands as the relay's own user:

  sudo -u twoegram-relay /opt/twoegram-relay/twoegram-relay upstream add claw1 192.168.1.20:8417
  sudo -u twoegram-relay /opt/twoegram-relay/twoegram-relay add "Apple IIe"
  sudo systemctl enable --now twoegram-relay
  sudo journalctl -u twoegram-relay -f
MSG

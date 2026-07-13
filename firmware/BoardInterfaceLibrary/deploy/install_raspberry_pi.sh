#!/usr/bin/env bash
set -euo pipefail

# Install the AEMS board daemon on a Raspberry Pi.
# Run from the BoardInterfaceLibrary directory:
#   sudo bash ./deploy/install_raspberry_pi.sh

if [[ "${EUID}" -ne 0 ]]; then
  echo "Run this installer with sudo." >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PACKAGE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
INSTALL_ROOT="/opt/aems"
VENV_DIR="${INSTALL_ROOT}/venv"

getent group aems >/dev/null 2>&1 || groupadd --system aems
id -u aems >/dev/null 2>&1 || useradd --system --gid aems --home-dir /var/lib/aems-server --shell /usr/sbin/nologin aems

mkdir -p "${INSTALL_ROOT}" /etc/aems-server /etc/aems-server/certs /var/lib/aems-server/captures /var/lib/aems-server/metadata /var/lib/aems-server/manifests /var/lib/aems-server/transfer_out
chown -R aems:aems /var/lib/aems-server
chown -R aems:aems /etc/aems-server/certs
if [[ -n "${SUDO_USER:-}" && "${SUDO_USER}" != "root" ]]; then
  usermod -aG aems "${SUDO_USER}"
fi

python3 -m venv "${VENV_DIR}"
"${VENV_DIR}/bin/pip" install --upgrade pip
"${VENV_DIR}/bin/pip" install "${PACKAGE_DIR}[daemon]"

cp "${SCRIPT_DIR}/config/aems-server.toml" /etc/aems-server/config.toml
cp "${SCRIPT_DIR}/systemd/aems-boardd.service" /etc/systemd/system/aems-boardd.service
cp "${SCRIPT_DIR}/systemd/aems-cloud-agent.service" /etc/systemd/system/aems-cloud-agent.service

systemctl daemon-reload
systemctl enable aems-boardd
systemctl restart aems-boardd

echo "AEMS daemon installed."
echo "Check status with: sudo systemctl status aems-boardd"
echo "Use CLI with: ${VENV_DIR}/bin/aemsctl server status"
echo "Optional cloud unit installed but not enabled: install cloud extras with '${VENV_DIR}/bin/pip install \".[cloud]\"', configure /etc/aems-server/cloud-agent.env, then enable aems-cloud-agent."
echo "If your user was added to group 'aems', log out/in once for socket permissions to refresh."

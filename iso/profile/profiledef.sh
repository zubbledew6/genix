#!/usr/bin/env bash
# shellcheck disable=SC2034

iso_name="genix"
iso_label="GENIX_$(date +%Y%m)"
iso_publisher="Genix"
iso_application="Genix Live Installer"
iso_version="$(date --date="@${SOURCE_DATE_EPOCH:-$(date +%s)}" +%Y.%m.%d)"
install_dir="genix"
buildmodes=('iso')
bootmodes=('bios.syslinux'
           'uefi.systemd-boot')
pacman_conf="pacman.conf"
airootfs_image_type="squashfs"
airootfs_image_tool_options=('-comp' 'xz' '-Xbcj' 'x86' '-b' '1M' '-Xdict-size' '1M')
bootstrap_tarball_compression=('zstd' '-c' '-T0' '--auto-threads=logical' '--long' '-19')
file_permissions=(
  ["/etc/shadow"]="0:0:400"
  ["/root"]="0:0:750"
  ["/root/.automated_script.sh"]="0:0:755"
  ["/root/.gnupg"]="0:0:700"
  ["/usr/local/bin/choose-mirror"]="0:0:755"
  ["/usr/local/bin/Installation_guide"]="0:0:755"
  ["/usr/local/bin/livecd-sound"]="0:0:755"
  ["/opt/genix/bin/genix-install"]="0:0:755"
  ["/opt/genix/bootstrap-build-tools.sh"]="0:0:755"
  ["/opt/genix/bootstrap-portage.sh"]="0:0:755"
  ["/opt/genix/build/genix-rebuild"]="0:0:755"
  ["/opt/genix/build/genix-render"]="0:0:755"
  ["/opt/genix/fastfetch/deploy-laptop.sh"]="0:0:755"
  ["/opt/genix/install.sh"]="0:0:755"
  ["/opt/genix/iso/build-live.sh"]="0:0:755"
  ["/opt/genix/iso/build.sh"]="0:0:755"
  ["/opt/genix/scripts/migrate-ext4-to-btrfs.sh"]="0:0:755"
  ["/root/.zlogin"]="0:0:755"
  ["/root/customize_airootfs.sh"]="0:0:755"
  ["/usr/bin/genix-install"]="0:0:755"
  ["/usr/bin/genix-rebuild"]="0:0:755"
  ["/usr/bin/genix-render"]="0:0:755"
  ["/usr/local/bin/genix-install"]="0:0:755"
)

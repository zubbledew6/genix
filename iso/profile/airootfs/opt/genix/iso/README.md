Genix live — boot this ISO on the **target laptop**, not your main PC.

The ISO is only a live environment. Nothing from it ends up on your disk:
the installer downloads a **Gentoo stage3** and builds the system from that.

| | |
|---|---|
| Base | Gentoo stage3 (`amd64`) |
| Init | **OpenRC** — no systemd |
| Root filesystem | btrfs, `@` subvolume |
| Bootloader | GRUB (UEFI or BIOS) |
| Packages | Portage, compile from source (`binary = false`) |

## Build (main PC, CachyOS/Arch)

```bash
cd ~/Projects/arch-hyprland/genix
sudo ./iso/build.sh
```

Output: `iso/out/genix-live.iso`. Needs `archiso`, `gcc`, `make`, network, and root.

## Flash USB

```bash
sudo dd if=iso/out/genix-live.iso of=/dev/sdX bs=4M status=progress conv=fsync
```

Balena Etcher works too — point it at `iso/out/genix-live.iso`.

## Install on target laptop

1. Boot the USB. You land on a root shell.
2. Get online — **the installer needs network throughout**:
   - ethernet should already work
   - wifi: `iwctl station wlan0 connect "SSID"`
3. Preview: `genix-install --dry-run`
4. Install: `genix-install`

The installer asks for the target disk, then how to lay it out, then hostname,
username, timezone, locale, keyboard layout, and the root and user passwords.
It refuses to install onto the live USB.

### Partitioning modes

| Mode | What it does |
|---|---|
| **Erase entire disk** | New GPT, ESP + btrfs. Everything on the disk is lost. |
| **Install alongside** | Creates one btrfs partition in the largest block of unallocated space and reuses the existing EFI partition. Other systems and their bootloaders are left alone. Needs at least 20 GiB free. |
| **Manual (cfdisk)** | Drops you into `cfdisk` to edit the partition table yourself, then asks which partition is root and which is the ESP. |

To dual-boot Windows, shrink the Windows partition **from inside Windows**
(Disk Management → Shrink Volume) before booting the USB, then pick
*Install alongside*. Leave the EFI partition unformatted when asked — that is
where the Windows bootloader lives.

In both dual-boot modes the installer enables `os-prober`, so the other systems
show up in the GRUB menu, and it does not overwrite the removable-media
fallback bootloader (`EFI/BOOT/BOOTX64.EFI`).

Budget 30–60 minutes: roughly 500 MB of stage3 plus the base packages.
Because it pulls prebuilt binaries from Gentoo's binhost, almost nothing
is compiled from source.

## After first boot

```bash
genix-rebuild doctor      # confirms emerge, init, config, generations
```

Then edit `/etc/genix/configuration.toml`, add atoms under `[packages] want`,
and run `genix-rebuild switch`.

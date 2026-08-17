#!/usr/bin/env python3
# live ISO installer. writes a gentoo stage3, not a copy of the live root.

import getpass
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
import tomllib
from pathlib import Path

MNT = Path("/mnt/genix-target")
GENIX_SRC = Path(os.environ.get("GENIX_SRC", "/tmp/genix"))
WORKDIR = Path("/tmp/genix-stage3")
DRAFT_CONFIG = Path("/tmp/genix-configuration.toml")

STAGE3_BASE = "https://distfiles.gentoo.org/releases/amd64/autobuilds"
STAGE3_POINTER = STAGE3_BASE + "/latest-stage3-amd64-openrc.txt"
BINHOST = "https://distfiles.gentoo.org/releases/amd64/binpackages/23.0/x86-64"
GENTOO_PROFILE = "default/linux/amd64/23.0"

ESP_GUID = "c12a7328-f81f-11d2-ba4b-00a0c93ec93b"
ESP_SIZE_MIB = 512
MIN_ROOT_MIB = 20 * 1024

HOSTNAME_DEFAULT = "genix"
USERNAME_DEFAULT = "genix"
TIMEZONE_DEFAULT = "Pacific/Auckland"
LOCALE_DEFAULT = "en_US.UTF-8"
KEYMAP_DEFAULT = "us"

USERNAME_RE = re.compile(r"^[a-z_][a-z0-9_-]*$")

# also dumped into the first generation so switch doesn't unmerge them
BASE_PACKAGES = [
    "app-admin/sudo",
    "app-editors/nano",
    "net-misc/dhcpcd",
    "net-misc/openssh",
    "net-wireless/iwd",
    "sys-boot/efibootmgr",
    "sys-boot/grub",
    "sys-fs/btrfs-progs",
    "sys-kernel/dracut",
    "sys-kernel/gentoo-kernel-bin",
    "sys-kernel/linux-firmware",
]

OPENRC_SERVICES = ["iwd", "dhcpcd", "sshd"]


class InstallError(RuntimeError):
    pass


def step(msg):
    print("\n%s" % msg, flush=True)


def sh(cmd, dry_run=False, check=True, env=None):
    print("+", " ".join(cmd), flush=True)
    if dry_run:
        return 0
    run_env = None
    if env:
        run_env = os.environ.copy()
        run_env.update(env)
    r = subprocess.run(cmd, env=run_env)
    if check and r.returncode != 0:
        raise InstallError("command failed (exit %d): %s" % (r.returncode, " ".join(cmd)))
    return r.returncode


def uefi_mode():
    return Path("/sys/firmware/efi").is_dir()


def human(mib):
    if mib >= 1024:
        return "%.1f GiB" % (mib / 1024.0)
    return "%d MiB" % mib


def parent_disk(part_name):
    node = Path("/sys/class/block") / part_name
    if not node.exists():
        return None
    holder = node.resolve().parent
    if holder.name != "block" and (holder / "dev").is_file():
        return holder.name
    return part_name


def busy_disks():
    # live usb + anything already mounted
    out = set()
    try:
        mounts = Path("/proc/mounts").read_text(encoding="utf-8").splitlines()
    except OSError:
        return out
    for line in mounts:
        fields = line.split()
        if not fields or not fields[0].startswith("/dev/"):
            continue
        disk = parent_disk(fields[0][len("/dev/"):])
        if disk:
            out.add(disk)
    return out


def list_disks():
    out = []
    block = Path("/sys/block")
    if not block.is_dir():
        return out
    busy = busy_disks()
    for p in sorted(block.iterdir()):
        name = p.name
        if not (name.startswith("sd") or name.startswith("nvme") or name.startswith("vd")):
            continue
        if name.endswith(("boot", "rpmb")):
            continue
        size = "(unknown size)"
        size_file = p / "size"
        if size_file.is_file():
            sectors = int(size_file.read_text().strip())
            if sectors == 0:
                continue
            size = "%.1f GiB" % ((sectors * 512) / (1024**3))
        model = ""
        model_file = p / "device" / "model"
        if model_file.is_file():
            model = model_file.read_text(errors="replace").strip()
        note = "in use (live media / mounted)" if name in busy else model
        out.append({"dev": str(Path("/dev") / name), "size": size, "note": note, "busy": name in busy})
    return out


def list_partitions(disk):
    try:
        raw = subprocess.check_output(
            ["lsblk", "-J", "-b", "-o", "PATH,SIZE,FSTYPE,LABEL,MOUNTPOINT,PARTTYPE", disk],
            text=True,
            stderr=subprocess.DEVNULL,
        )
    except (subprocess.CalledProcessError, OSError):
        return []
    out = []

    def add(node):
        # lsblk -J puts partitions next to the disk, not under children. annoying.
        path = node.get("path") or ""
        if not path or path == disk:
            return
        out.append(
            {
                "path": path,
                "size_mib": int(node.get("size") or 0) // (1024 * 1024),
                "fstype": node.get("fstype") or "",
                "label": node.get("label") or "",
                "mountpoint": node.get("mountpoint") or "",
                "parttype": (node.get("parttype") or "").lower(),
            }
        )

    for dev in json.loads(raw).get("blockdevices", []):
        add(dev)
        for child in dev.get("children") or []:
            add(child)
    out.sort(key=lambda p: p["path"])
    return out


def find_esp(disk):
    for part in list_partitions(disk):
        if part["parttype"] == ESP_GUID:
            return part
    return None


def free_regions(disk):
    try:
        raw = subprocess.check_output(
            ["parted", "-m", "-s", disk, "unit", "MiB", "print", "free"],
            text=True,
            stderr=subprocess.DEVNULL,
        )
    except (subprocess.CalledProcessError, OSError):
        return []
    out = []
    for line in raw.splitlines():
        fields = line.strip().rstrip(";").split(":")
        if len(fields) < 5 or fields[4] != "free":
            continue
        try:
            start = float(fields[1].rstrip("MiB"))
            end = float(fields[2].rstrip("MiB"))
        except ValueError:
            continue
        size = end - start
        if size >= 1:
            out.append({"start": int(start) + 1, "end": int(end), "size_mib": int(size)})
    return out


def largest_free_region(disk):
    regions = free_regions(disk)
    if not regions:
        return None
    return max(regions, key=lambda r: r["size_mib"])


def part_name(disk, index):
    if re.search(r"(nvme\d+n\d+|mmcblk\d+)$", disk):
        return "%sp%d" % (disk, index)
    return "%s%d" % (disk, index)


def wait_for_dev(path, dry_run, timeout=20):
    # parted returns before the node shows up
    if dry_run:
        return
    subprocess.run(["udevadm", "settle"], check=False)
    deadline = time.time() + timeout
    while time.time() < deadline:
        if Path(path).exists():
            return
        time.sleep(0.3)
        raise InstallError("partition %s never showed up (disk busy?)" % path)


def release_disk(disk, dry_run):
    if dry_run:
        print("release mounts and swap on", disk)
        return
    for line in Path("/proc/swaps").read_text(encoding="utf-8").splitlines()[1:]:
        fields = line.split()
        if fields and fields[0].startswith(disk):
            sh(["swapoff", fields[0]], dry_run, check=False)
    mounts = []
    for line in Path("/proc/mounts").read_text(encoding="utf-8").splitlines():
        fields = line.split()
        if len(fields) >= 2 and fields[0].startswith(disk):
            mounts.append(fields[1])
    for target in sorted(mounts, key=len, reverse=True):
        sh(["umount", "-R", target], dry_run, check=False)


def partition_wipe(disk, dry_run):
    release_disk(disk, dry_run)
    sh(["wipefs", "-a", disk], dry_run, check=False)
    sh(["parted", "-s", disk, "mklabel", "gpt"], dry_run)
    if uefi_mode():
        sh(["parted", "-s", disk, "mkpart", "ESP", "fat32", "1MiB", "%dMiB" % (ESP_SIZE_MIB + 1)], dry_run)
        sh(["parted", "-s", disk, "set", "1", "esp", "on"], dry_run)
        sh(["parted", "-s", disk, "mkpart", "root", "btrfs", "%dMiB" % (ESP_SIZE_MIB + 1), "100%"], dry_run)
        efi, root = part_name(disk, 1), part_name(disk, 2)
    else:
        sh(["parted", "-s", disk, "mkpart", "bios", "1MiB", "3MiB"], dry_run)
        sh(["parted", "-s", disk, "set", "1", "bios_grub", "on"], dry_run)
        sh(["parted", "-s", disk, "mkpart", "root", "btrfs", "3MiB", "100%"], dry_run)
        efi, root = None, part_name(disk, 2)
    sh(["partprobe", disk], dry_run, check=False)
    if efi:
        wait_for_dev(efi, dry_run)
        sh(["mkfs.vfat", "-F32", efi], dry_run)
    wait_for_dev(root, dry_run)
    return efi, root


def make_partition(disk, name, fstype, start_mib, end_mib, dry_run):
    before = {p["path"] for p in list_partitions(disk)}
    sh(
        ["parted", "-s", "--align", "optimal", disk, "mkpart", name, fstype,
         "%dMiB" % start_mib, "%dMiB" % end_mib],
        dry_run,
    )
    sh(["partprobe", disk], dry_run, check=False)
    if dry_run:
        return "%s<new>" % disk
    subprocess.run(["udevadm", "settle"], check=False)
    deadline = time.time() + 20
    while time.time() < deadline:
        new = {p["path"] for p in list_partitions(disk)} - before
        if len(new) == 1:
            path = new.pop()
            wait_for_dev(path, dry_run)
            return path
        time.sleep(0.3)
    raise InstallError("could not identify the partition just created on %s" % disk)


def partition_alongside(plan, dry_run):
    disk = plan["disk"]
    region = plan["region"]
    esp = plan["efi_part"]

    start = region["start"]
    end = region["end"]

    if uefi_mode() and not esp:
        esp_end = start + ESP_SIZE_MIB
        esp = make_partition(disk, "ESP", "fat32", start, esp_end, dry_run)
        idx = re.search(r"(\d+)$", esp)
        if idx and not dry_run:
            sh(["parted", "-s", disk, "set", idx.group(1), "esp", "on"], dry_run, check=False)
        sh(["mkfs.vfat", "-F32", esp], dry_run)
        start = esp_end

    root = make_partition(disk, "genix", "btrfs", start, end, dry_run)
    return esp, root


def apply_partitioning(plan, dry_run):
    if plan["mode"] == "wipe":
        return partition_wipe(plan["disk"], dry_run)
    if plan["mode"] == "alongside":
        return partition_alongside(plan, dry_run)
    # manual: partitions already chosen by the user
    efi = plan["efi_part"]
    root = plan["root_part"]
    release_disk(plan["disk"], dry_run)
    if efi and plan["format_efi"]:
        sh(["mkfs.vfat", "-F32", efi], dry_run)
    return efi, root


def setup_btrfs(root_part, dry_run):
    sh(["mkfs.btrfs", "-f", "-L", "genix-root", root_part], dry_run)
    if not dry_run:
        MNT.mkdir(parents=True, exist_ok=True)
    sh(["mount", root_part, str(MNT)], dry_run)
    sh(["btrfs", "subvolume", "create", str(MNT / "@")], dry_run)
    sh(["umount", str(MNT)], dry_run)
    sh(["mount", "-o", "subvol=@,compress=zstd", root_part, str(MNT)], dry_run)


def mount_efi(efi_part, dry_run):
    efi_mnt = MNT / "boot" / "efi"
    if not dry_run:
        efi_mnt.mkdir(parents=True, exist_ok=True)
    sh(["mount", efi_part, str(efi_mnt)], dry_run)


# stage3


def stage3_url(dry_run):
    if dry_run:
        return STAGE3_BASE + "/<latest>/stage3-amd64-openrc-<latest>.tar.xz"
    WORKDIR.mkdir(parents=True, exist_ok=True)
    pointer = WORKDIR / "latest-stage3.txt"
    sh(["curl", "-fsSL", "-o", str(pointer), STAGE3_POINTER])
    for line in pointer.read_text(encoding="utf-8").splitlines():
        fields = line.split()
        if fields and fields[0].endswith(".tar.xz"):
            return "%s/%s" % (STAGE3_BASE, fields[0])
    raise InstallError("could not find a stage3 tarball in %s" % STAGE3_POINTER)


def verify_stage3(tarball, url):
    result = subprocess.run(["curl", "-fsSL", url + ".DIGESTS"], capture_output=True, text=True)
    if result.returncode != 0:
        print("warning: could not fetch DIGESTS, skipping checksum")
        return
    lines = result.stdout.splitlines()
    want = None
    for i, line in enumerate(lines[:-1]):
        if line.strip() == "# SHA512 HASH":
            fields = lines[i + 1].split()
            if len(fields) == 2 and fields[1] == tarball.name:
                want = fields[0].lower()
                break
    if not want:
        print("warning: no SHA512 entry for %s, skipping checksum" % tarball.name)
        return
    digest = hashlib.sha512()
    with open(tarball, "rb") as f:
        for chunk in iter(lambda: f.read(4 * 1024 * 1024), b""):
            digest.update(chunk)
    if digest.hexdigest() != want:
        raise InstallError("stage3 checksum mismatch, redownload it")
    print("stage3 checksum ok")


def fetch_stage3(dry_run):
    url = stage3_url(dry_run)
    print("stage3:", url)
    tarball = WORKDIR / Path(url).name
    if dry_run:
        print("download and verify", tarball)
        return tarball
    if not tarball.is_file():
        sh(["curl", "-fL", "--progress-bar", "-o", str(tarball), url])
    else:
        print("reusing already downloaded", tarball)
    verify_stage3(tarball, url)
    return tarball


def extract_stage3(tarball, dry_run):
    sh(
        ["tar", "xpf", str(tarball), "--xattrs-include=*.*", "--numeric-owner", "-C", str(MNT)],
        dry_run,
    )


# chroot


def prepare_chroot(dry_run):
    for d in ("dev", "proc", "sys", "run"):
        dst = MNT / d
        if not dry_run:
            dst.mkdir(parents=True, exist_ok=True)
        sh(["mount", "--rbind", str(Path("/") / d), str(dst)], dry_run)
        sh(["mount", "--make-rslave", str(dst)], dry_run, check=False)
    if dry_run:
        print("copy /etc/resolv.conf into target")
    else:
        shutil.copy2("/etc/resolv.conf", MNT / "etc" / "resolv.conf")


def cleanup_chroot(dry_run):
    for d in ("run", "sys", "proc", "dev"):
        sh(["umount", "-R", str(MNT / d)], dry_run, check=False)
    efi = MNT / "boot" / "efi"
    if dry_run or os.path.ismount(efi):
        sh(["umount", str(efi)], dry_run, check=False)


def chroot_cmd(cmd, dry_run, check=True):
    return sh(["chroot", str(MNT)] + cmd, dry_run, check=check)


def write_file(path, text, dry_run, mode=None):
    if dry_run:
        print("write", path)
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    if mode is not None:
        path.chmod(mode)


# portage


def stage3_make_conf_vars():
    # keep whatever the stage3 already set
    values = {"COMMON_FLAGS": "-O2 -pipe", "CHOST": "x86_64-pc-linux-gnu"}
    make_conf = MNT / "etc" / "portage" / "make.conf"
    if not make_conf.is_file():
        return values
    text = make_conf.read_text(encoding="utf-8", errors="replace")
    for key in ("COMMON_FLAGS", "CHOST"):
        found = re.search(r'^%s="([^"]*)"' % key, text, flags=re.M)
        if found:
            values[key] = found.group(1)
    return values


def portage_settings(dry_run):
    jobs = os.cpu_count() or 4
    return {
        "makeopts": "-j%d" % jobs,
        # both platforms so the official binhost grub matches
        "grub_platforms": "efi-64 pc",
        "vars": {"COMMON_FLAGS": "-O2 -pipe", "CHOST": "x86_64-pc-linux-gnu"}
        if dry_run
        else stage3_make_conf_vars(),
    }


def load_toml(path):
    with open(path, "rb") as f:
        return tomllib.load(f)


def atom_name(item):
    if isinstance(item, dict):
        return (item.get("name") or item.get("atom") or "").strip()
    s = str(item).strip()
    for prefix in ("bin>", "bin:", "src>", "src:"):
        if s.startswith(prefix):
            return s[len(prefix):].strip()
    return s


def want_atoms(data):
    want = ((data.get("packages") or {}).get("want")) or []
    out = []
    seen = set()
    for item in want:
        name = atom_name(item)
        if name and name not in seen:
            seen.add(name)
            out.append(name)
    for p in BASE_PACKAGES:
        if p not in seen:
            seen.add(p)
            out.append(p)
    return out


def portage_from_toml(data, settings, install_binary=None):
    system = data.get("system") or {}
    portage = system.get("portage") or {}
    use = system.get("use") or ["-systemd", "elogind"]
    if isinstance(use, list):
        use_s = " ".join(str(x) for x in use)
    else:
        use_s = str(use)
    features = portage.get("features") or "parallel-fetch"
    if install_binary is not None:
        binary = bool(install_binary)
    else:
        binary = bool(portage.get("binary", False))
    vars_ = dict(settings["vars"])
    for key, val in (portage.get("vars") or {}).items():
        vars_[key] = str(val)
    return {
        "use": use_s,
        "features": features,
        "binary": binary,
        "makeopts": portage.get("makeopts") or settings["makeopts"],
        "binhost": portage.get("binhost") or BINHOST,
        "vars": vars_,
        "grub_platforms": vars_.get("GRUB_PLATFORMS") or settings["grub_platforms"],
        "pkg_use": (data.get("packages") or {}).get("use") or {},
        "want": want_atoms(data),
    }


def configure_portage(portage, dry_run):
    features = portage["features"]
    feat_bits = [f for f in features.split() if f]
    emerge_opts = ""
    if portage["binary"]:
        if "getbinpkg" not in feat_bits:
            feat_bits.append("getbinpkg")
        emerge_opts = 'EMERGE_DEFAULT_OPTS="--getbinpkg"\n'
    else:
        feat_bits = [f for f in feat_bits if f != "getbinpkg"]
    features = " ".join(feat_bits) or "parallel-fetch"

    text = (
        'COMMON_FLAGS="%s"\n'
        'CFLAGS="${COMMON_FLAGS}"\n'
        'CXXFLAGS="${COMMON_FLAGS}"\n'
        'FCFLAGS="${COMMON_FLAGS}"\n'
        'FFLAGS="${COMMON_FLAGS}"\n'
        'CHOST="%s"\n'
        'MAKEOPTS="%s"\n'
        'USE="%s"\n'
        'ACCEPT_KEYWORDS="amd64"\n'
        # "*/*" is wrong here, firmware stays masked
        'ACCEPT_LICENSE="* -@EULA"\n'
        'GRUB_PLATFORMS="%s"\n'
        'FEATURES="%s"\n'
        'PORTAGE_BINHOST="%s"\n'
        "%s"
        "LC_MESSAGES=C.utf8\n"
        % (
            portage["vars"].get("COMMON_FLAGS", "-O2 -pipe"),
            portage["vars"].get("CHOST", "x86_64-pc-linux-gnu"),
            portage["makeopts"],
            portage["use"],
            portage["grub_platforms"],
            features,
            portage["binhost"],
            emerge_opts,
        )
    )
    write_file(MNT / "etc" / "portage" / "make.conf", text, dry_run)

    use_lines = ["sys-kernel/installkernel dracut\n"]
    for atom, flags in portage["pkg_use"].items():
        if atom == "sys-kernel/installkernel":
            continue
        if isinstance(flags, list):
            flag_s = " ".join(str(x) for x in flags)
        else:
            flag_s = str(flags)
        use_lines.append("%s %s\n" % (atom, flag_s))
    write_file(
        MNT / "etc" / "portage" / "package.use" / "genix-base",
        "".join(use_lines),
        dry_run,
    )
    write_file(
        MNT / "etc" / "portage" / "package.license" / "genix-base",
        "sys-kernel/linux-firmware linux-fw-redistributable no-source-code\n"
        "sys-firmware/* linux-fw-redistributable no-source-code\n",
        dry_run,
    )
    write_file(
        MNT / "etc" / "portage" / "repos.conf" / "gentoo.conf",
        "[DEFAULT]\nmain-repo = gentoo\n\n"
        "[gentoo]\nlocation = /var/db/repos/gentoo\n"
        "sync-type = rsync\nsync-uri = rsync://rsync.gentoo.org/gentoo-portage\n"
        "auto-sync = yes\n",
        dry_run,
    )
    # hostonly would bake the live usb's hardware into the initramfs
    write_file(
        MNT / "etc" / "dracut.conf.d" / "genix.conf",
        'hostonly="no"\ncompress="zstd"\nadd_dracutmodules+=" btrfs "\n',
        dry_run,
    )


def emerge_base(plan, portage, dry_run):
    chroot_cmd(["emerge-webrsync"], dry_run)
    chroot_cmd(["eselect", "profile", "set", GENTOO_PROFILE], dry_run, check=False)
    chroot_cmd(["eselect", "profile", "show"], dry_run, check=False)

    # os-prober wants grub[mount]. binhost grub doesn't have it. mask first.
    write_file(
        MNT / "etc" / "portage" / "package.mask" / "genix-os-prober",
        "sys-boot/os-prober\n",
        dry_run,
    )
    pkgs = portage["want"]
    cmd = ["emerge", "--verbose", "--noreplace"]
    if portage["binary"]:
        cmd.append("--getbinpkg")
        print("emerging from binhost")
    else:
        cmd.append("--getbinpkg=n")
        print("compiling from source (binary = false)")
    chroot_cmd(cmd + pkgs, dry_run)

    if plan.get("dual_boot"):
        write_file(
            MNT / "etc" / "portage" / "package.use" / "genix-grub",
            "sys-boot/grub mount\n",
            dry_run,
        )
        mask = MNT / "etc" / "portage" / "package.mask" / "genix-os-prober"
        if dry_run:
            print("rm", mask)
        elif mask.is_file():
            mask.unlink()
        print("rebuilding grub with USE=mount")
        chroot_cmd(
            ["emerge", "--verbose", "--oneshot", "--getbinpkg=n", "sys-boot/grub"],
            dry_run,
        )
        chroot_cmd(
            ["emerge", "--verbose", "--noreplace", "sys-boot/os-prober"],
            dry_run,
            check=False,
        )

    if dry_run:
        return
    boot = MNT / "boot"
    if not list(boot.glob("vmlinuz*")):
        raise InstallError("no kernel in /boot after emerging gentoo-kernel-bin")
    if not list(boot.glob("initramfs*")):
        raise InstallError("no initramfs in /boot, dracut didn't run")


# users / fstab / etc


def write_fstab(root_part, efi_part, dry_run):
    uuid_root = "ROOT_UUID"
    uuid_efi = "EFI_UUID"
    if not dry_run:
        uuid_root = subprocess.check_output(
            ["blkid", "-s", "UUID", "-o", "value", root_part], text=True
        ).strip()
        if efi_part:
            uuid_efi = subprocess.check_output(
                ["blkid", "-s", "UUID", "-o", "value", efi_part], text=True
            ).strip()
    lines = [
        "# root subvol comes from GRUB rootflags= (not fstab)",
        "UUID=%s  /  btrfs  compress=zstd  0 0" % uuid_root,
    ]
    if efi_part:
        lines.append("UUID=%s  /boot/efi  vfat  defaults  0 2" % uuid_efi)
    write_file(MNT / "etc" / "fstab", "\n".join(lines) + "\n", dry_run)


def configure_system(cfg, dry_run):
    hostname = cfg["hostname"]
    write_file(MNT / "etc" / "hostname", hostname + "\n", dry_run)
    write_file(MNT / "etc" / "conf.d" / "hostname", 'hostname="%s"\n' % hostname, dry_run)
    write_file(
        MNT / "etc" / "hosts",
        "127.0.0.1\tlocalhost\n::1\t\tlocalhost\n127.0.1.1\t%s.localdomain\t%s\n" % (hostname, hostname),
        dry_run,
    )
    write_file(MNT / "etc" / "conf.d" / "keymaps", 'keymap="%s"\n' % cfg["keymap"], dry_run)
    write_file(MNT / "etc" / "timezone", cfg["timezone"] + "\n", dry_run)
    write_file(MNT / "etc" / "locale.gen", "%s UTF-8\n" % cfg["locale"], dry_run)
    write_file(
        MNT / "etc" / "env.d" / "02locale",
        'LANG="%s"\nLC_COLLATE="C.UTF-8"\n' % cfg["locale"],
        dry_run,
    )
    write_file(MNT / "etc" / "sudoers.d" / "10-wheel", "%wheel ALL=(ALL:ALL) ALL\n", dry_run, mode=0o440)

    if not dry_run:
        localtime = MNT / "etc" / "localtime"
        localtime.unlink(missing_ok=True)
        localtime.symlink_to("/usr/share/zoneinfo/%s" % cfg["timezone"])

    chroot_cmd(["emerge", "--config", "sys-libs/timezone-data"], dry_run, check=False)
    chroot_cmd(["locale-gen"], dry_run, check=False)
    chroot_cmd(["env-update"], dry_run, check=False)

    username = cfg["username"]
    useradd = ["useradd", "-m", "-G", "wheel,users,audio,video", "-s", "/bin/bash", username]
    if dry_run:
        chroot_cmd(useradd, dry_run)
        print("set passwords for root and", username)
        return
    if chroot_cmd(["id", "-u", username], dry_run, check=False) != 0:
        chroot_cmd(useradd, dry_run)
    for account, password in (("root", cfg["root_password"]), (username, cfg["user_password"])):
        proc = subprocess.run(
            ["chroot", str(MNT), "chpasswd"], input="%s:%s\n" % (account, password), text=True
        )
        if proc.returncode != 0:
            raise InstallError("failed to set password for %s" % account)


def enable_services(dry_run):
    for svc in OPENRC_SERVICES:
        chroot_cmd(["rc-update", "add", svc, "default"], dry_run, check=False)


def install_bootloader(plan, dry_run):
    dual = plan["dual_boot"]
    grub_default = (
        'GRUB_DISTRIBUTOR="Genix"\n'
        "GRUB_TIMEOUT=5\n"
        "GRUB_TIMEOUT_STYLE=menu\n"
        'GRUB_CMDLINE_LINUX="rootflags=subvol=@"\n'
        'GRUB_CMDLINE_LINUX_DEFAULT="loglevel=3"\n'
        'GRUB_PRELOAD_MODULES="part_gpt part_msdos btrfs"\n'
        "GRUB_DISABLE_OS_PROBER=%s\n" % ("false" if dual else "true")
    )
    write_file(MNT / "etc" / "default" / "grub", grub_default, dry_run)

    if uefi_mode():
        chroot_cmd(
            ["grub-install", "--target=x86_64-efi", "--efi-directory=/boot/efi", "--bootloader-id=Genix"],
            dry_run,
        )
        # don't overwrite EFI/BOOT/BOOTX64.EFI if windows is on this disk
        if not dual:
            chroot_cmd(
                ["grub-install", "--target=x86_64-efi", "--efi-directory=/boot/efi", "--removable"],
                dry_run,
                check=False,
            )
    else:
        chroot_cmd(["grub-install", "--target=i386-pc", plan["disk"]], dry_run)
    chroot_cmd(["grub-mkconfig", "-o", "/boot/grub/grub.cfg"], dry_run)

    if dry_run:
        return
    grub_cfg = MNT / "boot" / "grub" / "grub.cfg"
    if not grub_cfg.is_file():
        raise InstallError("grub.cfg was not generated")
    text = grub_cfg.read_text(encoding="utf-8", errors="replace")
    if "menuentry " not in text:
        raise InstallError("grub.cfg has no menuentry, you'd just get the firmware menu")
    if dual:
        others = len(re.findall(r"^menuentry ", text, flags=re.M)) - 1
        print("grub: %d other os entries" % max(0, others))


# seed /etc/genix


def genix_config(cfg, settings):
    want = "\n".join('  "%s",' % p for p in BASE_PACKAGES)
    services = ", ".join('"%s"' % s for s in OPENRC_SERVICES)
    pkg_use = '"sys-kernel/installkernel" = ["dracut"]\n'
    if cfg.get("dual_boot"):
        pkg_use += '"sys-boot/grub" = ["mount"]\n'
    return (
        "# binary = false compiles. makeopts is -j. use is global USE.\n"
        "# packages.use is per-atom. add atoms under packages.want.\n\n"
        "[system]\n"
        'hostname = "%s"\n'
        'use = ["-systemd", "elogind"]\n\n'
        "[system.identity]\n"
        'name = "Genix"\n'
        'pretty_name = "Genix"\n'
        'id = "genix"\n'
        'id_like = "gentoo"\n'
        'version = "0.1"\n'
        'version_id = "0.1"\n\n'
        "[system.portage]\n"
        'accept_keywords = "amd64"\n'
        'makeopts = "%s"\n'
        'features = "parallel-fetch"\n'
        'binhost = "%s"\n'
        "binary = false\n"
        "lfs = false\n\n"
        "[system.portage.vars]\n"
        'COMMON_FLAGS = "%s"\n'
        'CFLAGS = "${COMMON_FLAGS}"\n'
        'CXXFLAGS = "${COMMON_FLAGS}"\n'
        'CHOST = "%s"\n'
        'GRUB_PLATFORMS = "%s"\n'
        'ACCEPT_LICENSE = "* -@EULA"\n\n'
        "[packages]\n"
        "want = [\n%s\n]\n\n"
        "[packages.use]\n"
        "%s\n"
        "[services]\n"
        "enable = [%s]\n\n"
        "[system.boot]\n"
        "enabled = true\n"
        'backend = "grub"\n'
        'subvol_prefix = "@genix-"\n'
        'default_subvol = "@"\n'
        % (
            cfg["hostname"],
            settings["makeopts"],
            BINHOST,
            settings["vars"]["COMMON_FLAGS"],
            settings["vars"]["CHOST"],
            settings["grub_platforms"],
            want,
            pkg_use,
            services,
        )
    )


def ensure_binary_false(text):
    if re.search(r"(?m)^binary\s*=", text):
        return re.sub(r"(?m)^binary\s*=.*$", "binary = false", text)
    marker = "[system.portage]\n"
    if marker in text:
        return text.replace(marker, marker + "binary = false\n", 1)
    return text + "\n[system.portage]\nbinary = false\n"


def seed_genix(cfg, settings, dry_run):
    dst = MNT / "tmp" / "genix"
    if dry_run:
        print("copy genix from", GENIX_SRC, "to", dst)
    else:
        if not GENIX_SRC.is_dir():
            raise InstallError("GENIX_SRC missing: %s" % GENIX_SRC)
        shutil.copytree(GENIX_SRC, dst, dirs_exist_ok=True)
        # squashfs often strips +x
        for script in [dst / "install.sh"] + sorted((dst / "bin").glob("*")):
            if script.is_file():
                script.chmod(0o755)
    chroot_cmd(["/bin/bash", "/tmp/genix/install.sh"], dry_run)
    src = Path(cfg.get("config_path") or DRAFT_CONFIG)
    if src.is_file():
        text = ensure_binary_false(src.read_text(encoding="utf-8"))
    else:
        text = genix_config(cfg, settings)
    write_file(MNT / "etc" / "genix" / "configuration.toml", text, dry_run)


def copy_wifi(dry_run):
    src = Path("/var/lib/iwd")
    dst = MNT / "var" / "lib" / "iwd"
    if not src.is_dir():
        print("no live iwd profiles")
        return
    files = [
        p for p in src.rglob("*")
        if p.is_file() and p.suffix in (".psk", ".open", ".8021x")
    ]
    if not files:
        print("no saved wifi on the live system")
        return
    if dry_run:
        print("copy wifi:", [str(p.relative_to(src)) for p in files])
        return
    dst.mkdir(parents=True, exist_ok=True)
    for p in files:
        rel = p.relative_to(src)
        target = dst / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(p, target)
        os.chmod(target, 0o600)
        print("wifi:", rel)
    live_conf = Path("/etc/iwd/main.conf")
    if live_conf.is_file():
        conf_dir = MNT / "etc" / "iwd"
        conf_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(live_conf, conf_dir / "main.conf")


def maybe_edit_config(path):
    print()
    print("wrote %s" % path)
    print("yes: edit USE, -j, packages — install compiles from source")
    print("no:  skip edit — fast install from binary packages")
    print("     (after reboot, genix-rebuild still compiles from source)")
    if not ask_yesno("Edit configuration.toml before installing?", default=False):
        print("using binary packages for this install")
        return False
    editor = os.environ.get("EDITOR") or ""
    if not editor:
        for cand in ("nano", "vim", "vi"):
            if shutil.which(cand):
                editor = cand
                break
    if not editor:
        print("no editor found — falling back to binary install")
        return False
    subprocess.run([editor, str(path)])
    try:
        load_toml(path)
    except Exception as exc:
        raise InstallError("configuration.toml is invalid: %s" % exc)
    print("compiling from source with your configuration")
    return True


def run_install(plan, cfg, dry_run):
    cfg["dual_boot"] = bool(plan.get("dual_boot"))
    print("disk:", plan["disk"])
    print("layout:", plan["summary"])
    print("host:", cfg["hostname"], "  firmware:", "UEFI" if uefi_mode() else "BIOS")
    if dry_run:
        print("dry-run, no writes")

    step("partitions on %s" % plan["disk"])
    efi, root = apply_partitioning(plan, dry_run)
    print("root:", root)
    print("efi:", efi or "(bios)")

    step("btrfs @")
    setup_btrfs(root, dry_run)
    if efi:
        mount_efi(efi, dry_run)

    step("stage3")
    tarball = fetch_stage3(dry_run)
    extract_stage3(tarball, dry_run)
    write_fstab(root, efi, dry_run)

    prepare_chroot(dry_run)
    try:
        settings = portage_settings(dry_run)
        data = load_toml(cfg.get("config_path") or DRAFT_CONFIG)
        install_binary = cfg.get("install_binary", False)
        portage = portage_from_toml(data, settings, install_binary=install_binary)
        step("portage")
        configure_portage(portage, dry_run)
        emerge_base(plan, portage, dry_run)
        copy_wifi(dry_run)
        step("users / locale")
        configure_system(cfg, dry_run)
        enable_services(dry_run)
        step("genix")
        seed_genix(cfg, settings, dry_run)
        step("grub")
        install_bootloader(plan, dry_run)
        chroot_cmd(["genix-rebuild", "switch", "--no-emerge"], dry_run, check=False)
    finally:
        cleanup_chroot(dry_run)
        sh(["umount", "-R", str(MNT)], dry_run, check=False)

    if dry_run:
        print("\ndry-run done")
        return
    print("\ndone. reboot.")
    print("login: %s or root" % cfg["username"])


def short_dev(dev):
    return dev[5:] if dev.startswith("/dev/") else dev


def ask(prompt, default=""):
    while True:
        try:
            if default:
                raw = input("%s [%s]: " % (prompt, default)).strip()
            else:
                raw = input("%s: " % prompt).strip()
        except EOFError:
            raise KeyboardInterrupt
        if raw:
            return raw
        if default:
            return default
        print("Please retry.")


def ask_yesno(prompt, default=False):
    hint = "Y/n" if default else "y/N"
    while True:
        try:
            raw = input("%s [%s] " % (prompt, hint)).strip().lower()
        except EOFError:
            raise KeyboardInterrupt
        if not raw:
            return default
        if raw in ("y", "yes"):
            return True
        if raw in ("n", "no"):
            return False
        print("Please reply 'y' or 'n'")


def ask_password(who):
    print("Changing password for %s" % who)
    while True:
        first = getpass.getpass("New password: ")
        if not first:
            print("Password cannot be empty")
            continue
        if first != getpass.getpass("Retype password: "):
            print("Passwords do not match. Please retry.")
            continue
        return first


def describe_disk(d):
    note = d["note"] or ""
    extra = (" " + note) if note else ""
    return "%s\t(%s%s)" % (short_dev(d["dev"]), d["size"], extra)


def describe_part(p):
    bits = [human(p["size_mib"])]
    bits.append(p["fstype"] or "unformatted")
    if p["label"]:
        bits.append('"%s"' % p["label"])
    if p["parttype"] == ESP_GUID:
        bits.append("EFI system partition")
    if p["mountpoint"]:
        bits.append("mounted at %s" % p["mountpoint"])
    return "%s\t(%s)" % (short_dev(p["path"]), " ".join(bits))


def ask_which(prompt, names, default, help_text, aliases=None):
    aliases = aliases or {}
    valid = set(names) | set(aliases)
    while True:
        raw = ask(prompt, default)
        if raw == "?":
            print(help_text)
            continue
        key = short_dev(raw)
        if key in aliases:
            return aliases[key]
        if key in valid:
            return key
        print("'%s' is not a valid choice. Type '?' for help." % raw)


COMMON_KEYMAPS = (
    "us uk de fr es it ru dvorak colemak"
)


def ask_keymap(default):
    help_text = "Common layouts: %s\nEnter a kbd layout name (usually a 2-letter country code)." % COMMON_KEYMAPS
    while True:
        raw = ask("Select keyboard layout ('?' for list)", default)
        if raw == "?":
            print(help_text)
            continue
        if re.match(r"^[a-zA-Z0-9][a-zA-Z0-9_-]*$", raw):
            return raw
        print("Please retry.")


def ask_timezone(default):
    zoneinfo = Path("/usr/share/zoneinfo")
    while True:
        raw = ask("Which timezone are you in? ('?' for list)", default)
        if raw == "?":
            if zoneinfo.is_dir():
                regions = sorted(
                    p.name for p in zoneinfo.iterdir()
                    if p.is_dir() and p.name[0].isupper() and p.name not in ("posix", "right")
                )
                print("\n".join(regions))
            else:
                print("Example: Pacific/Auckland")
            continue
        path = zoneinfo / raw
        if path.is_file():
            return raw
        if path.is_dir():
            cities = sorted(
                p.relative_to(path).as_posix()
                for p in path.rglob("*")
                if p.is_file() and not p.name.startswith(".")
            )
            print("Select a timezone in %s:" % raw)
            print("\n".join(cities[:100]))
            if cities:
                default = "%s/%s" % (raw, cities[0])
            continue
        print("Unknown timezone. Type '?' for a list of regions.")


def ask_settings(dry_run):
    cfg = {}

    while True:
        raw = ask("Enter system hostname (short form, e.g. 'genix')", HOSTNAME_DEFAULT)
        if re.match(r"^[a-zA-Z0-9][a-zA-Z0-9-]*$", raw):
            cfg["hostname"] = raw
            break
        print("letters, digits and dashes only. Please retry.")

    cfg["keymap"] = ask_keymap(KEYMAP_DEFAULT)

    while True:
        raw = ask("Enter username", USERNAME_DEFAULT)
        if USERNAME_RE.match(raw):
            cfg["username"] = raw
            break
        print("lowercase letters, digits, dash and underscore only. Please retry.")
    if dry_run:
        cfg["user_password"] = ""
        cfg["root_password"] = ""
    else:
        cfg["user_password"] = ask_password(cfg["username"])
        cfg["root_password"] = ask_password("root")

    cfg["timezone"] = ask_timezone(detect_timezone())
    cfg["locale"] = ask("Enter locale", LOCALE_DEFAULT)
    return cfg


def detect_timezone():
    link = Path("/etc/localtime")
    if link.is_symlink():
        target = os.readlink(link)
        if "zoneinfo/" in target:
            return target.split("zoneinfo/", 1)[1]
    return TIMEZONE_DEFAULT


DISK_HELP = """\
Enter the disk name as listed above (sda, nvme0n1, …).
Busy disks are the live USB or something already mounted. Don't pick those.
Enter 'none' to abort.
"""

MODE_HELP = """\
sys         Erase the disk. New GPT, ESP + btrfs @  (Alpine's 'sys' mode)
alongside   Use free space. Keeps existing systems and their bootloader
cfdisk      Partition yourself, then pick root and ESP
"""


def default_disk(disks):
    for d in disks:
        if not d["busy"]:
            return short_dev(d["dev"])
    return "none"


def ask_disk_plan(disks, force):
    by_name = {short_dev(d["dev"]): d for d in disks}

    print("Available disks are:")
    for d in disks:
        print("  %s" % describe_disk(d))

    names = list(by_name) + ["none"]
    while True:
        pick = ask_which(
            "Which disk would you like to use? (or '?' for help or 'none')",
            names,
            default_disk(disks),
            DISK_HELP,
        )
        if pick == "none":
            return None
        disk = by_name[pick]
        if disk["busy"] and not force:
            print(
                "%s is in use (live usb / mounted). pick another disk.\n"
                "--force if you really mean it." % disk["dev"]
            )
            continue
        break

    print("The following disk is selected:")
    print("  %s" % describe_disk(disk))

    parts = list_partitions(disk["dev"])
    region = largest_free_region(disk["dev"])
    has_free = bool(region and region["size_mib"] >= MIN_ROOT_MIB)

    while True:
        mode = ask_which(
            "How would you like to use it? ('sys', 'alongside', 'cfdisk' or '?' for help)",
            ["sys", "alongside", "cfdisk"],
            "sys",
            MODE_HELP,
            aliases={"wipe": "sys", "manual": "cfdisk"},
        )

        plan = {
            "mode": None, "disk": disk["dev"], "root_part": None, "efi_part": None,
            "format_efi": False, "dual_boot": False, "region": None, "summary": "",
        }

        if mode == "sys":
            plan["mode"] = "wipe"
            plan["summary"] = "erase %s, new GPT, ESP + btrfs @" % disk["dev"]
            print("The following disk will be erased and used:")
            print("  %s" % describe_disk(disk))
            if not ask_yesno("WARNING: Erase the above disk and continue?", default=False):
                return None
            return plan

        if mode == "alongside":
            if not has_free:
                if region:
                    print(
                        "Only %s free, need at least %s. Shrink a partition or use cfdisk."
                        % (human(region["size_mib"]), human(MIN_ROOT_MIB))
                    )
                else:
                    print("No unallocated space on this disk. Shrink a partition or use cfdisk.")
                continue
            plan["mode"] = "alongside"
            plan["region"] = region
            plan["dual_boot"] = True
            esp = find_esp(disk["dev"])
            plan["efi_part"] = esp["path"] if esp else None
            plan["format_efi"] = False
            plan["summary"] = "new %s btrfs partition in free space, existing systems kept" % human(
                region["size_mib"]
            )
            print("Using %s of free space. Existing systems will be kept." % human(region["size_mib"]))
            if not ask_yesno("WARNING: Create a new partition and continue?", default=False):
                return None
            return plan

        break

    print("\ncfdisk %s" % disk["dev"])
    print("Create or resize partitions, then Write and Quit.\n")
    time.sleep(1)
    sh(["cfdisk", disk["dev"]], check=False)
    sh(["partprobe", disk["dev"]], check=False)
    subprocess.run(["udevadm", "settle"], check=False)

    parts = list_partitions(disk["dev"])
    if not parts:
        print("No partitions found after cfdisk.")
        return None

    print("Available partitions are:")
    for p in parts:
        print("  %s" % describe_part(p))

    part_names = [short_dev(p["path"]) for p in parts]
    by_part = {short_dev(p["path"]): p for p in parts}
    root_name = ask_which(
        "Which partition should be used as root? (it will be formatted btrfs)",
        part_names,
        part_names[-1],
        "Enter a partition name from the list (e.g. %s)." % part_names[0],
    )
    root = by_part[root_name]
    plan["mode"] = "manual"
    plan["root_part"] = root["path"]

    if uefi_mode():
        others = [p for p in parts if p["path"] != root["path"]]
        print("Available EFI candidates:")
        if others:
            for p in others:
                print("  %s" % describe_part(p))
        else:
            print("  (none)")
        efi_names = [short_dev(p["path"]) for p in others] + ["none"]
        guessed = None
        for p in others:
            if p["parttype"] == ESP_GUID:
                guessed = short_dev(p["path"])
                break
        efi_name = ask_which(
            "Which partition is the EFI system partition? (or 'none' or '?')",
            efi_names,
            guessed or "none",
            "Pick the existing ESP for dual boot. 'none' if this machine has no EFI partition.",
        )
        if efi_name == "none":
            plan["efi_part"] = None
        else:
            esp = by_part[efi_name]
            plan["efi_part"] = esp["path"]
            looks_used = bool(esp["fstype"]) and esp["parttype"] == ESP_GUID
            plan["format_efi"] = not ask_yesno(
                "Keep existing files on %s? (recommended for dual boot)" % esp["path"],
                default=looks_used,
            )

    occupied = [
        p for p in parts
        if p["path"] not in (plan["root_part"], plan["efi_part"]) and p["fstype"]
    ]
    plan["dual_boot"] = bool(occupied)
    plan["summary"] = "manual: root=%s efi=%s%s" % (
        plan["root_part"],
        plan["efi_part"] or "none",
        " (formatting ESP)" if plan["format_efi"] else "",
    )
    if not ask_yesno("WARNING: Continue with this layout?", default=False):
        return None
    return plan


def have_network():
    return subprocess.run(
        ["curl", "-fsS", "--max-time", "15", "-o", "/dev/null", STAGE3_POINTER]
    ).returncode == 0


def main():
    args = sys.argv[1:]
    dry_run = "--dry-run" in args
    force = "--force" in args

    if os.geteuid() != 0:
        sys.stderr.write("run as root\n")
        return 1
    for tool in ("parted", "mkfs.btrfs", "curl", "tar", "blkid", "chroot", "lsblk"):
        if not shutil.which(tool):
            sys.stderr.write("missing required tool: %s\n" % tool)
            return 1

    if not dry_run and not have_network():
        sys.stderr.write(
            "\nno network. This installer downloads Gentoo, so connect first:\n"
            "  ethernet: should already work\n"
            '  wifi:     iwctl station wlan0 connect "SSID"\n'
        )
        return 1

    disks = list_disks()
    if not disks:
        sys.stderr.write("no disks found\n")
        return 1

    if not (sys.stdin.isatty() and sys.stdout.isatty()):
        sys.stderr.write("need a tty\n")
        return 1

    try:
        plan = ask_disk_plan(disks, force)
        if plan is None:
            print("aborted")
            return 1
        cfg = ask_settings(dry_run)
        cfg["dual_boot"] = bool(plan.get("dual_boot"))
        DRAFT_CONFIG.write_text(genix_config(cfg, portage_settings(True)), encoding="utf-8")
        edited = maybe_edit_config(DRAFT_CONFIG)
        cfg["install_binary"] = not edited
        data = load_toml(DRAFT_CONFIG)
        host = ((data.get("system") or {}).get("hostname") or "").strip()
        if host:
            cfg["hostname"] = host
        cfg["config_path"] = str(DRAFT_CONFIG)
    except KeyboardInterrupt:
        print("\naborted")
        return 1

    try:
        run_install(plan, cfg, dry_run)
    except InstallError as exc:
        sys.stderr.write("\nerror: %s\n" % exc)
        return 1
    except KeyboardInterrupt:
        sys.stderr.write("\ninterrupted\n")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

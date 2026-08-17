# iso / live stick

boot this on the laptop you want genix on. not your daily driver.

github: https://github.com/zubbledew6/genix  
site: https://genix.hoi-hoi33666.workers.dev/

the stick is just a live env. install downloads a gentoo stage3 and builds from that — it does **not** copy the live root onto disk.

| | |
|---|---|
| base | gentoo stage3 amd64 |
| init | openrc |
| root | btrfs `@` |
| boot | grub |
| pkgs | portage (bins if you skip config edit, source if you edit) |

## build

needs archiso, gcc, make, net, root:

```bash
cd ~/Projects/arch-hyprland/genix
sudo ./iso/build.sh
```

output: `iso/out/genix-live.iso`

## flash

```bash
sudo dd if=iso/out/genix-live.iso of=/dev/sdX bs=4M status=progress conv=fsync
```

etcher works too.

## install

1. boot usb → root shell
2. network (installer needs it the whole time)
   - ethernet usually fine
   - wifi: `iwctl station wlan0 connect "SSID"`
3. `genix-install --dry-run` if you want a peek
4. `genix-install`

asks disk layout, hostname, user, passwords, etc. wont install onto the usb itself.

disk modes:
- **sys** — wipe whole disk, new gpt + esp + btrfs
- **alongside** — free space ≥20G, reuse esp, leave other oss alone
- **cfdisk** — you partition, then pick root/esp

dual boot windows: shrink windows from windows first, then alongside. dont format the esp if windows lives there.

expect a while — stage3 + base packages. saying no to editing config uses the gentoo binhost so its mostly downloads not compiles.

## after reboot

```bash
genix-rebuild doctor
# edit /etc/genix/configuration.toml
genix-rebuild switch
```

# daily driver notes

dont dump 40 packages at once. add a few, `switch`, see if it boots. generations are there if it goes bad.

starter list lives in `example/configuration-daily.toml`.

## easy ones

```
app-misc/fastfetch
app-misc/tmux
sys-apps/man-pages
app-archives/unzip
```

```bash
genix-rebuild switch --dry-run
genix-rebuild switch
```

## network stuff

do one package at a time. if something needs real deps:

```toml
{ name = "net-wireless/wpa_supplicant", nodeps = false }
```

on lfs with `lfs = true` that can get ugly. expected.

## config only (no emerge)

```bash
genix-rebuild switch --no-emerge
```

rewrites portage files / os-release / services and saves a gen. skips emerge.

## desktop

hyprland / full xorg on a thin lfs base is a long weekend (or more). one package at a time. browser last.

when you trust portage more:

```toml
[system.portage]
lfs = false
```

## copy tree to another box

```bash
tar -C ~/Projects/arch-hyprland -cf - \
  --exclude='genix/iso/work' --exclude='genix/build' \
  genix | ssh user@LAPTOP 'rm -rf /tmp/genix && tar -xf - -C /tmp'
```

then on that box as root: `cd /tmp/genix && ./install.sh`

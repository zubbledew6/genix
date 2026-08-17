# fastfetch

two logos in `fastfetch/`:

- `ascii-art-40-440.txt` — big one, `install.sh` drops it in `~/.config/fastfetch/`
- `genix-compact.txt` — smaller, for an upstream PR (they want under ~50x20)

`$1` in the ascii is color 1. we use mauve `38;2;192;132;184` (same as ansi_color in os-release).

## upstream (later)

maintainers check that HOME_URL actually loads. so wait until the real site exists.

1. open a logo request issue on https://github.com/fastfetch-cli/fastfetch
2. paste `/etc/os-release` from a genix box (need ID=genix, LOGO=genix, HOME_URL)
3. attach `genix-compact.txt`
4. PR that adds the logo + detects `ID=genix`

example os-release bits:

```
NAME="Genix"
ID=genix
ID_LIKE=gentoo
HOME_URL="https://genix.hoi-hoi33666.workers.dev/"
LOGO=genix
ANSI_COLOR="38;2;192;132;184"
```

test local:

```bash
fastfetch --logo-type file --logo-source fastfetch/genix-compact.txt \
  --logo-color-1 '38;2;192;132;184'
```

copy to a machine:

```bash
./fastfetch/deploy-laptop.sh user@host
```

# pushing genix to github

the real code lives here:

`~/Projects/arch-hyprland/genix/`

github is https://github.com/zubbledew6/genix — files sit at the **root** of that repo (not inside another `genix/` folder). thats why we use subtree instead of just `git push`.

site right now is a placeholder:
https://genix.hoi-hoi33666.workers.dev/

cloudflare runs `npx wrangler deploy` and serves `website/`.

## how to push

```bash
cd ~/Projects/arch-hyprland

git add genix/
git commit -m "whatever you changed"

git branch -D genix-main
git subtree split --prefix=genix -b genix-main
git push https://github.com/zubbledew6/genix.git genix-main:main
```

if github complains about history, slap `--force` on the push. only do that when you mean it.

web people: edit stuff under `genix/website/`, same push path.

## iso release (when you actually want one)

```bash
cd ~/Projects/arch-hyprland/genix
sudo ./iso/build.sh
sha256sum iso/out/*.iso
```

then on github: Releases → new release → tag like `v0.1.0` → upload the iso.

## fastfetch

dont bother upstream until the real site exists and `HOME_URL` works. notes in `FASTFETCH.md`.

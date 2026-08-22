# Deploying the MM1-BLACK installer

Static page on GitHub Pages; firmware binaries come from **GitHub Releases** (not committed to git).

## Setup

1. Repository **public** (`verlab/mm1-black`).
2. **Settings → Pages → Source: GitHub Actions**.
3. URL: **https://verlab.github.io/mm1-black/**

## How downloads work

Browsers cannot `fetch()` release files directly from `github.com` (CORS). The **Deploy flasher** workflow copies each release `.bin` into `bins/` on the Pages site (build-time only). The installer downloads from the same origin (`./bins/MM1-BLACK-denky32-v*.bin` and `./bins/MM1-BLACK-mm1_p4-v*.bin`). It also writes `latest.json` so the device can check for a new release over STA Wi-Fi.

After a new tag release, Pages redeploys automatically when the **Release** workflow finishes (or on push to `main`). Manual: **Actions → Deploy flasher → Run workflow**.

# Deploying the MM1-BLACK installer

Static page on GitHub Pages; firmware binaries come from **GitHub Releases** (not committed to git).

## Setup

1. Repository **public** (`Mira-Robotica/mm1-black`).
2. **Settings → Pages → Source: GitHub Actions**.
3. Product URL: **https://mira-robotica.github.io/mm1-black/**
4. Firmware installer: **https://mira-robotica.github.io/mm1-black/firmware/**

## How downloads work

Browsers cannot `fetch()` release files directly from `github.com` (CORS). The Pages workflow publishes the product at the root, the installer in `firmware/`, and each release `.bin` in root `bins/` (build-time only). The installer downloads from the same origin (`../bins/MM1-BLACK-denky32-v*.bin` and `../bins/MM1-BLACK-mm1_p4-v*.bin`). Root `latest.json` remains available so deployed devices can check for a new release over STA Wi-Fi.

After a new tag release, Pages redeploys automatically when the **Release** workflow finishes (or on push to `main`). Manual: **Actions → Deploy flasher → Run workflow**.

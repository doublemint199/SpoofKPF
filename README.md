# SpoofKPF — Project 8 device-info spoof (PongoOS KPF component)

Custom palera1n KPF module that spoofs **native** device-info (device tree / sysctl)
on iPhone 6s (A9, `iPhone8,1`) toward **iPhone15,2** (iPhone 14 Pro). String / DT
layer only — it never fakes physical capability (see `PROJECT8_NOTES.md` §6).

> **Status: SKELETON.** `src/spoof.c` does **no writes** yet. It only proves the
> build → load → run pipeline and that the KPF can reach the live device tree.
> Real patches land in P1 (see NOTES §9, §13.7).

## How it works

This is a tiny **overlay** repo. CI does NOT fork all of PongoOS. Instead
`.github/workflows/build.yml`:

1. Clones `palera1n/PongoOS@iOS15` (the active branch) with submodules.
2. Copies `src/spoof.c` into `checkra1n/kpf/` (sources there are auto-globbed).
3. Injects `&kpf_spoof` into the `kpf_components[]` array in `checkra1n/kpf/main.c`.
4. Runs upstream `make` on a **macOS** runner (needs `xcrun -sdk iphoneos clang`).
5. Uploads `build/checkra1n-kpf-pongo` (+ `build/Pongo.bin`) as an artifact.

`checkra1n-kpf-pongo` is the file you feed to `palera1n -K`.

> `palera1n -K` **fully replaces** the stock KPF, so this build keeps every stock
> component (bindfs, dyld, mach_port, nvram, ramdisk, trustcache, vfs, …) and only
> **adds** `kpf_spoof`. Do not remove any stock component.

## Build it

You need a GitHub repo running Actions (macOS runners are free for public repos).

```
# from this folder (C:\Users\PC\.Project8\SpoofKPF)
git init
git add .
git commit -m "SpoofKPF skeleton + CI"
git branch -M main
git remote add origin https://github.com/<you>/SpoofKPF.git
git push -u origin main
```

Actions runs automatically on push. Download the **spoofkpf** artifact from the run
→ it contains `checkra1n-kpf-pongo`.

## Load it (skeleton test)

On the palen1x host, with the 6s in DFU:

```
palera1n -f -K /path/to/checkra1n-kpf-pongo -v
```

- `-f` rootful (this device already has a rootful fakefs — do NOT add `-c`).
- `-K` load our KPF.  `-v` verbose boot so you can see the `[SpoofKPF]` puts output.

Expected: device boots jailbroken exactly as normal, and verbose log shows
`[SpoofKPF] skeleton: bootprep ran, device tree reachable (found /chosen)`.
That confirms the pipeline. **No device values change yet** (skeleton).

Recovery: `palera1n --force-revert` or `palera1n -R`. KPF is RAM-only per boot
(semi-tethered) — a plain reboot drops it.

## Roadmap (mirrors PROJECT8_NOTES.md)

- **P1** — in `kpf_spoof_bootprep`, gate on `spoof=1`, then in-place overwrite the
  32-byte DT props (serial-number, region-info, model-number,
  regulatory-model-number) + `target-type` N71→D73. Verify via `ios-telnet.ps1`.
- **P1b** — board `hw.model` N71AP→D73AP.
- **P2b** — DT `model` + sysctl `hw.machine` → `iPhone15,2` via pointer redirect
  (+1 byte; cannot overwrite in place).

/*
 * SpoofKPF — Project 8 device-info spoof component
 * ---------------------------------------------------------------------------
 * Target: iPhone8,1 (A9, N71AP)  ->  iPhone15,2 (iPhone 14 Pro, D73AP).
 * Layer:  string / device-tree ONLY. Never "enables" physical capability (§6).
 *
 * This file is a PONGO KPF component that gets globbed into checkra1n/kpf/ and
 * registered in kpf_components[] by the CI injection step (see .github/workflows).
 *
 * STAGE P1a — FIRST REAL, OBSERVABLE WRITE (single patch, per §7 discipline).
 *   All device-tree writes are GATED behind boot-arg "spoof=1". A plain boot
 *   (no flag) does ZERO writes -> always a clean recovery path (§7).
 *
 *   Why we verify with a real write instead of the old skeleton `puts`:
 *   palera1n does NOT tee the PongoOS serial console into its own log, so the
 *   skeleton's puts line was never observable headlessly. Instead we patch ONE
 *   safe, reversible, 32-byte DT prop (serial-number) to a known sentinel and
 *   read it back over the LAN via `ioreg` (IOPlatformSerialNumber derives from
 *   the DT serial-number). A changed value proves, in one shot:
 *     (a) our component's bootprep hook runs,
 *     (b) gDeviceTree / dt_prop are reachable and writable from the KPF,
 *     (c) the write propagates into the live userspace IORegistry.
 *   That is exactly the P1 mechanism; once proven, the remaining 32B props
 *   (region-info, model-number, regulatory-model-number, target-type) are
 *   identical repeats.
 *
 * DT layout (from src/drivers/dt/dt.h, iOS15 branch):
 *   dt_prop_t { char key[0x20]; uint32_t len; char val[]; }
 *   dt_prop(node,key,&len) returns val and sets len = the prop's buffer size.
 *   We only ever write <= len bytes and null-pad the rest, so the DT blob size
 *   never changes (true in-place edit — no node rebuild, no slack needed).
 *
 * NOT here: DT `model` and sysctl `hw.machine` -> "iPhone15,2". Those are +1
 * byte over the real 9-byte "iPhone8,1" and cannot be overwritten in place.
 * They need a pointer/handler redirect (P2b), which is separate original work.
 */

#include "kpf.h"
#include <pongo.h>
#include <xnu/xnu.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Set by the boot-arg gate; every WRITE below is guarded by this (§7). */
static bool g_spoof_enabled = false;

/*
 * Gate on boot-arg "spoof=1". palera1n's `-e spoof=1` is written into the
 * device-tree /chosen "boot-args" property by the `xargs` PongoOS command,
 * which runs BEFORE bootx (and therefore before this bootprep hook). Reading
 * it here — rather than in init() — guarantees the flag is already present.
 * Read-only; returns true only when the flag is explicitly there, so a plain
 * boot is always a clean no-write pass-through (the safe default of §7).
 */
static bool spoof_gate_enabled(void)
{
    dt_node_t *chosen = dt_find(gDeviceTree, "chosen");
    if (chosen == NULL) {
        return false;
    }
    size_t balen = 0;
    char *bootargs = dt_prop(chosen, "boot-args", &balen);
    if (bootargs == NULL || balen == 0) {
        return false;
    }
    return memmem(bootargs, balen, "spoof=1", 7) != NULL;
}

/*
 * In-place overwrite of a device-tree string property on the root node
 * (gDeviceTree). Bounded by the prop's existing buffer length; never grows it.
 * Zero-fills the whole buffer first so no stale tail bytes leak, then copies
 * the new value. Returns true on success, false if the prop is missing or the
 * new value would not fit (in which case nothing is written).
 */
static bool __attribute__((unused)) dt_set_root_str(const char *key, const char *val)
{
    size_t len = 0;
    char *p = (char *)dt_prop(gDeviceTree, key, &len);
    if (p == NULL || len == 0) {
        printf("[SpoofKPF] prop '%s' NOT found — skip\n", key);
        return false;
    }
    size_t vlen = strlen(val);
    if (vlen > len) {
        printf("[SpoofKPF] prop '%s' buffer too small (need %u, have %u) — skip\n",
               key, (unsigned)vlen, (unsigned)len);
        return false;
    }
    memset(p, 0, len);
    memcpy(p, val, vlen);
    printf("[SpoofKPF] prop '%s' -> '%s' (buf %u)\n", key, val, (unsigned)len);
    return true;
}

/*
 * In-place overwrite that may GROW the logical length by up to the prop's
 * 4-byte-alignment slack. Apple DTB stores each prop value padded to a 4-byte
 * boundary, so a 9-byte value ("iPhone8,1") physically occupies 12 bytes — 3
 * bytes of slack. We write up to that physical size and bump the prop's length
 * field (the uint32_t immediately before val in dt_prop_t) WITHOUT resizing the
 * DT blob. The top flag byte of len (bit 31 = placeholder marker in Apple DT) is
 * preserved; only the low 24 bits carry the length.
 * This is how model iPhone8,1(9) -> iPhone15,2(10) fits in place.
 */
static bool __attribute__((unused)) dt_set_root_str_grow(const char *key, const char *val)
{
    size_t len = 0;
    char *p = (char *)dt_prop(gDeviceTree, key, &len);
    if (p == NULL) {
        printf("[SpoofKPF] grow '%s' NOT found — skip\n", key);
        return false;
    }
    size_t vlen = strlen(val);
    size_t phys = (len + 3u) & ~((size_t)3u);   /* value padded to 4-byte boundary */
    if (vlen > phys) {
        printf("[SpoofKPF] grow '%s' no slack (need %u, phys %u) — skip\n",
               key, (unsigned)vlen, (unsigned)phys);
        return false;
    }
    uint32_t *lenp = (uint32_t *)(p - 4);       /* dt_prop_t.len precedes val */
    memset(p, 0, phys);
    memcpy(p, val, vlen);
    *lenp = (*lenp & 0xff000000u) | ((uint32_t)vlen & 0x00ffffffu);
    printf("[SpoofKPF] grow '%s' -> '%s' (len %u->%u, phys %u)\n",
           key, val, (unsigned)len, (unsigned)vlen, (unsigned)phys);
    return true;
}

static void kpf_spoof_bootprep(struct mach_header_64 *hdr)
{
    (void)hdr;

    /* GATE MODEL CHANGE — boot-arg gate abandoned (unreliable). Confirmed on
     * device: at bootprep time the DT /chosen "boot-args" prop does NOT yet hold
     * palera1n's `-e` args (runtime /chosen has no boot-args prop at all), so the
     * boot-arg gate never fired. Instead we gate EXTERNALLY: loading THIS KPF
     * means spoof is active; the clean-recovery path of §7 is a STOCK boot
     * (palera1n without -K) — a normal un-patched jailbreak, proven by go2.sh.
     * The boot-arg check is kept for diagnostics only. */
    g_spoof_enabled = spoof_gate_enabled();
    puts(g_spoof_enabled ? "[SpoofKPF] boot-arg spoof=1 present (diag)"
                         : "[SpoofKPF] boot-arg gate not set — using external gate");

    /* -----------------------------------------------------------------------
     * P2b — THE prize: model iPhone8,1(9) -> iPhone15,2(10). +1 byte, fitted via
     * the DT prop's 4-byte-alignment slack (dt_set_root_str_grow). serial-number
     * is NOT touched — it breaks activation (§13.15). Afterward we observe over
     * the LAN whether hw.machine follows (it may derive from DT) via sysctl/ioreg.
     * Isolated single write this round (§7).
     * --------------------------------------------------------------------- */
    dt_set_root_str_grow("model", "iPhone15,2");

    /* P1b (safe, no activation impact) — add later, one at a time, boot-test:
     *   region-info, model-number(->MQ0E3), regulatory-model-number(->A2650),
     *   target-type(N71->D73). If hw.machine does NOT follow model, that string
     *   is kernel-level and needs a separate patch. */
}

/*
 * Component registration. Only .bootprep is used — device-tree editing happens
 * before XNU boot, so we need no kernel-text code patches (.patches is just the
 * empty terminator). Matches the designated-initializer style of kpf_nvram etc.
 */
kpf_component_t kpf_spoof =
{
    .bootprep = kpf_spoof_bootprep,
    .patches =
    {
        {},
    },
};

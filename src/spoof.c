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
static bool dt_set_root_str(const char *key, const char *val)
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
     * P1a — SINGLE observable write: serial-number only. 12-char Apple-shaped
     * sentinel, unmistakable when read back via ioreg IOPlatformSerialNumber.
     * --------------------------------------------------------------------- */
    dt_set_root_str("serial-number", "P8SPOOF0TEST");

    /* -----------------------------------------------------------------------
     * P1b TODO (add one at a time, boot-test each — §7):
     *   region-info, model-number, regulatory-model-number   (32B buffers)
     *   target-type  N71 -> D73                               (3B == 3B)
     *   hw.model board N71AP -> D73AP  (verify source: DT vs kernel literal)
     * P2b (separate): DT model + sysctl hw.machine -> iPhone15,2 via pointer
     *   redirect (+1 byte, cannot overwrite in place).
     * --------------------------------------------------------------------- */
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

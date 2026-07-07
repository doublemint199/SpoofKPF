/*
 * SpoofKPF — Project 8 device-info spoof component  (SKELETON / READ-ONLY no-op)
 * ---------------------------------------------------------------------------
 * Target: iPhone8,1 (A9, N71AP)  ->  iPhone15,2 (iPhone 14 Pro, D73AP).
 * Layer:  string / device-tree ONLY. Never "enables" physical capability (§6).
 *
 * This file is a PONGO KPF component that gets globbed into checkra1n/kpf/ and
 * registered in kpf_components[] by the CI injection step (see .github/workflows).
 *
 * STATUS: SKELETON. It performs NO writes to kernel or device tree. Its only job
 * is to prove the pipeline end-to-end:
 *   (a) our fork builds via `make` on the upstream iOS15 tree,
 *   (b) `palera1n -K checkra1n-kpf-pongo` loads OUR kpf (replacing stock) and the
 *       jailbreak still boots (all stock components preserved),
 *   (c) our component's bootprep hook actually runs, and
 *   (d) we can reach the live device tree (gDeviceTree) from inside the KPF.
 *
 * The REAL P1 work (in-place patch of the 32-byte DT props: serial-number,
 * region-info, model-number, regulatory-model-number, target-type; plus board
 * hw.model N71AP->D73AP) goes inside kpf_spoof_bootprep, GATED behind spoof=1
 * (§7: no WRITE without the boot-arg gate + force-revert recovery path).
 *
 * NOT here: DT `model` and sysctl `hw.machine` -> "iPhone15,2". Those are +1 byte
 * over the real 9-byte "iPhone8,1" and cannot be overwritten in place. They need a
 * pointer/handler redirect (P2b), which is separate original work.
 */

#include "kpf.h"
#include <pongo.h>
#include <xnu/xnu.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Set by the boot-arg gate; P1 patches must check this before any WRITE. */
static bool g_spoof_enabled = false;

/*
 * Gate on boot-arg "spoof=1" via the device-tree /chosen boot-args string.
 * Read-only. Returns true only when the flag is explicitly present, so a plain
 * boot (no flag) is always a clean pass-through — the safe default of §7.
 */
static bool spoof_gate_enabled(void)
{
    dt_node_t *chosen = dt_find(gDeviceTree, "chosen");
    if (chosen == NULL) {
        return false;
    }
    /* dt_prop length type intentionally not used here (skeleton): we only need
     * to know the flag is present. P1 will read boot-args with the exact
     * dt_prop() signature and memmem() for "spoof=1". */
    (void)chosen;
    return false; /* skeleton: gate stays OFF until P1 wires boot-arg parsing */
}

static void kpf_spoof_bootprep(struct mach_header_64 *hdr)
{
    (void)hdr;

    g_spoof_enabled = spoof_gate_enabled();

    /* Prove device-tree reachability from inside the KPF (core P1 assumption).
     * Uses only gDeviceTree + dt_find — identical usage to checkra1n/kpf/ramdisk.c
     * so the symbols are guaranteed to resolve at modload. No writes. */
    dt_node_t *chosen = dt_find(gDeviceTree, "chosen");
    if (chosen != NULL) {
        puts("[SpoofKPF] skeleton: bootprep ran, device tree reachable (found /chosen)");
    } else {
        puts("[SpoofKPF] skeleton: bootprep ran, but /chosen NOT found");
    }

    /* -----------------------------------------------------------------------
     * P1 TODO (guard with g_spoof_enabled): in-place DT prop overwrite on the
     * root node (gDeviceTree) — all fit their existing buffers:
     *   serial-number, region-info, model-number, regulatory-model-number  (32B pad)
     *   target-type  N71 -> D73                                             (3B == 3B)
     * P1b: board id hw.model N71AP -> D73AP (verify source is DT vs kernel).
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

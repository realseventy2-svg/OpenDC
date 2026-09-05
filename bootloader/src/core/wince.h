#ifndef WINCE_H
#define WINCE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Microsoft Windows CE ROMHDR signature: "ECEC" (0x43454345) */
#define WINCE_ROMHDR_SIG    0x43454345UL

/* Standard Windows CE sector 0 header address */
#define WINCE_HDR_BASE      0x8CE01000UL

/* Direct GD-ROM entrypoint used by Windows CE */
#define WINCE_GD_ENTRYPOINT 0x8C0010F0UL

/* Microsoft Windows CE ROMHDR structure */
typedef struct {
    uint32_t dllfirst;
    uint32_t dlllast;
    uint32_t physfirst;
    uint32_t physlast;
    uint32_t nummods;
    uint32_t ulRAMStart;
    uint32_t ulRAMFree;
    uint32_t ulRAMEnd;
    uint32_t ulCopyEntries;
    uint32_t ulCopyOffset;
    uint32_t ulProfileLen;
    uint32_t ulProfileOffset;
    uint32_t numfiles;
    uint32_t ulKernelFlags;
    uint32_t ulFSRamPercent;
    uint32_t ulDbgApiGlobal;
    uint32_t ulBootKey;
    uint32_t ulFileName;
} wince_romhdr_t;

/**
 * Detect if the currently loaded disc image or buffer is a Windows CE title.
 * Checks both the provided buffer and the standard WinCE header mirror at 0x8CE01000.
 */
int wince_detect(const uint8_t *sec0_header);

/**
 * Returns 1 if a Windows CE disc has been detected and is active, 0 otherwise.
 */
int wince_is_active(void);

/**
 * Compute the required SB_GDSTARD register value for Windows CE.
 * If WinCE is active and has a valid header, returns wince_hdr[3] + wince_hdr[4].
 * If Katana/standard, returns the Katana default 0x0C110000.
 */
uint32_t wince_get_gdstard(void);

/**
 * Apply the 98-word security checksum required by Midway and San Francisco Rush
 * around entrypoint 0x8C0010F0.
 */
void wince_apply_security_checksum(void);

/**
 * Configure the Windows CE runtime environment:
 * - Computes and sets SB_GDSTARD
 * - Applies security checksums
 * - Sets up any CE-specific hardware state
 */
void wince_setup_env(void);

#ifdef __cplusplus
}
#endif

#endif /* WINCE_H */

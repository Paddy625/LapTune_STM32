FatFs R0.16, vendored from STMicroelectronics/stm32-mw-fatfs at
`dd9126e957ae841a13ccbe2115d39a1fb7414126` (source/ff.c, ff.h, diskio.h, ffunicode.c).
Copyright and license notices are preserved in the original files and LICENSE.md.
ffconf.h is the upstream template configured for FAT12/16/32, 512-byte sectors,
static LFN buffer (FF_USE_LFN=1, FF_MAX_LFN=63, code page 437), one volume,
shared sector buffer, no exFAT or automatic format. LFN supports SESSION.json
and per-boot directories; no dynamic allocation is enabled.
FF_USE_MKFS can be overridden only for the host filesystem integration tests.

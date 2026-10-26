FatFs R0.16, vendored from STMicroelectronics/stm32-mw-fatfs at
`dd9126e957ae841a13ccbe2115d39a1fb7414126` (source/ff.c, ff.h, diskio.h).
Copyright and license notices are preserved in the original files and LICENSE.md.
ffconf.h is the upstream template configured for FAT12/16/32, 512-byte sectors,
short ASCII names, one volume, shared sector buffer, no exFAT or automatic format.
FF_USE_MKFS can be overridden only for the host filesystem integration tests.

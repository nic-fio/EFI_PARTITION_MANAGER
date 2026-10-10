/* Copyright (c) 2026 Nicola Fiorillo
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */

/* The glyphs of the graphical interface (tools/gen-font.py), linked in as
 * read-only data; gfx.c reads them. */
    .section .rodata
    .balign 16
    .globl font_inter_data, font_inter_end
    .hidden font_inter_data, font_inter_end
font_inter_data:
    .incbin "src/partmgr/font_inter.bin"
font_inter_end:
    .section .note.GNU-stack, "", @progbits

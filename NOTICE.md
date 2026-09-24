# Copyright and third-party notices

EFI Partition Manager
Copyright (c) 2026 nic-fio

Licensed under the **Apache License 2.0 with the "Commons Clause" License
Condition v1.0**. See [LICENSE](LICENSE).

## In short

| You may | You may not |
|---|---|
| Use EFI Partition Manager for anything, privately or at work, in a company or at home, free of charge | **Sell** it, or sell a product or service whose value comes, entirely or substantially, from it |
| Copy it and give it to anyone, unchanged, free of charge | Charge a fee for it, or for hosting, support or consulting that is substantially about it |
| Modify it for your own use | Remove the copyright, licence and Commons Clause notices |
| Share your modified version, free of charge, keeping the notices and stating what you changed | |

Using EFI Partition Manager as a tool while doing paid work (repairing a
computer, preparing disks for customers) is allowed: what is forbidden is making
money **from EFI Partition Manager itself**.

**Want to sell it, or include it in a product you sell?** That needs a separate,
commercial licence: open an issue at
https://github.com/nic-fio/EFI_PARTITION_MANAGER/issues and ask.

Suggested attribution for a modified version:

> Based on EFI Partition Manager by nic-fio
> (https://github.com/nic-fio/EFI_PARTITION_MANAGER), Apache 2.0 with Commons
> Clause. Changes were made.

## Third-party components in this repository

| Component | Licence |
|---|---|
| `docs/assets/vendor/mermaid.min.js` (diagrams in the manuals) | MIT, see `docs/assets/vendor/mermaid.LICENSE` |
| `src/partmgr/font_inter.bin` (the glyphs of the Inter font, rendered by `tools/gen-font.py` and built into `partmgr.efi`) | SIL Open Font License 1.1, reproduced below |

These components keep their own licences: the terms above apply to EFI
Partition Manager itself, not to them.

## The Inter font

`partmgr.efi` draws its graphical interface with glyphs rendered from the
Inter typeface, version 4.1 (https://github.com/rsms/inter). Its copyright
notice and licence:

```
Copyright (c) 2016 The Inter Project Authors (https://github.com/rsms/inter)

This Font Software is licensed under the SIL Open Font License, Version 1.1.
This license is copied below, and is also available with a FAQ at:
http://scripts.sil.org/OFL

-----------------------------------------------------------
SIL OPEN FONT LICENSE Version 1.1 - 26 February 2007
-----------------------------------------------------------

PREAMBLE
The goals of the Open Font License (OFL) are to stimulate worldwide
development of collaborative font projects, to support the font creation
efforts of academic and linguistic communities, and to provide a free and
open framework in which fonts may be shared and improved in partnership
with others.

The OFL allows the licensed fonts to be used, studied, modified and
redistributed freely as long as they are not sold by themselves. The
fonts, including any derivative works, can be bundled, embedded,
redistributed and/or sold with any software provided that any reserved
names are not used by derivative works. The fonts and derivatives,
however, cannot be released under any other type of license. The
requirement for fonts to remain under this license does not apply
to any document created using the fonts or their derivatives.

DEFINITIONS
"Font Software" refers to the set of files released by the Copyright
Holder(s) under this license and clearly marked as such. This may
include source files, build scripts and documentation.

"Reserved Font Name" refers to any names specified as such after the
copyright statement(s).

"Original Version" refers to the collection of Font Software components as
distributed by the Copyright Holder(s).

"Modified Version" refers to any derivative made by adding to, deleting,
or substituting -- in part or in whole -- any of the components of the
Original Version, by changing formats or by porting the Font Software to a
new environment.

"Author" refers to any designer, engineer, programmer, technical
writer or other person who contributed to the Font Software.

PERMISSION AND CONDITIONS
Permission is hereby granted, free of charge, to any person obtaining
a copy of the Font Software, to use, study, copy, merge, embed, modify,
redistribute, and sell modified and unmodified copies of the Font
Software, subject to the following conditions:

1) Neither the Font Software nor any of its individual components,
in Original or Modified Versions, may be sold by itself.

2) Original or Modified Versions of the Font Software may be bundled,
redistributed and/or sold with any software, provided that each copy
contains the above copyright notice and this license. These can be
included either as stand-alone text files, human-readable headers or
in the appropriate machine-readable metadata fields within text or
binary files as long as those fields can be easily viewed by the user.

3) No Modified Version of the Font Software may use the Reserved Font
Name(s) unless explicit written permission is granted by the corresponding
Copyright Holder. This restriction only applies to the primary font name as
presented to the users.

4) The name(s) of the Copyright Holder(s) or the Author(s) of the Font
Software shall not be used to promote, endorse or advertise any
Modified Version, except to acknowledge the contribution(s) of the
Copyright Holder(s) and the Author(s) or with their explicit written
permission.

5) The Font Software, modified or unmodified, in part or in whole,
must be distributed entirely under this license, and must not be
distributed under any other license. The requirement for fonts to
remain under this license does not apply to any document created
using the Font Software.

TERMINATION
This license becomes null and void if any of the above conditions are
not met.

DISCLAIMER
THE FONT SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT
OF COPYRIGHT, PATENT, TRADEMARK, OR OTHER RIGHT. IN NO EVENT SHALL THE
COPYRIGHT HOLDER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
INCLUDING ANY GENERAL, SPECIAL, INDIRECT, INCIDENTAL, OR CONSEQUENTIAL
DAMAGES, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF THE USE OR INABILITY TO USE THE FONT SOFTWARE OR FROM
OTHER DEALINGS IN THE FONT SOFTWARE.
```

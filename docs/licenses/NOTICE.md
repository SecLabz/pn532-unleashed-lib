# Third-party notices

This file retains component copyrights, license terms and provenance.
The project is distributed under GPL-3.0-or-later; see [LICENSE](../../LICENSE).
Include the applicable notices with redistributed source and binaries.

## Pico SDK and pico-examples — BSD-3-Clause

Both components carry the identical notice below.

```text
Copyright 2020 (c) 2020 Raspberry Pi (Trading) Ltd.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
   disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following
   disclaimer in the documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## TinyUSB — MIT

```text
The MIT License (MIT)

Copyright (c) 2018, hathach (tinyusb.org)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

## Proxmark3 / Crapto1 — GPL-3.0-or-later

```text
Proxmark3 / Crapto1 notices
=========================

Reviewed upstream revision:
https://github.com/RfidResearchGroup/proxmark3/tree/2bf48a02c8f45af06e9811f18f37348facf25292

The Crypto1 primitives and recovery core are adapted for C/RP2040 and
Rust. The ReaderMifare Darkside collector is adapted for bounded direct RF.
These adaptations are distributed under GPL-3.0-or-later; the upstream
copyrights remain with their respective holders. Provenance notices were
updated on 2026-09-27. See LICENSE for the complete GNU GPL version 3.

From common/crapto1/crypto1.c, crapto1.c and crapto1.h:

Copyright (C) 2008-2014 bla <blapost@gmail.com>
Copyright (C) Proxmark3 contributors. See AUTHORS.md for details.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

See LICENSE.txt for the text of the license.

From armsrc/iso14443a.c (ReaderMifare collector):

Copyright (C) Jonathan Westhues, Nov 2006
Copyright (C) Gerhard de Koning Gans - May 2008
Copyright (C) Proxmark3 contributors. See AUTHORS.md for details.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

See LICENSE.txt for the text of the license.

From client/src/mifare/mifarehost.c (historical Nethemba credit):

Borrowed initially from https://nethemba.com/tag/darkside-attack/
Copyright (C) mifare@nethemba.com, 2010
Copyright (C) Proxmark3 contributors. See AUTHORS.md for details.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

See LICENSE.txt for the text of the license.

The Nethemba notice above records the reviewed PM3 file provenance; it
does not assert that every function in that file was copied. MFOC and
MFCUK also distribute Crapto1 with earlier GPL-2.0-or-later notices. The
reviewed PM3 sources used for these attributions state GPL-3.0-or-later.
```

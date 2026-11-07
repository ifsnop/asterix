asterix
=======

[![Build Status](https://travis-ci.org/CroatiaControlLtd/asterix.svg?branch=master)](https://travis-ci.org/CroatiaControlLtd/asterix)
[![Documentation badge](https://readthedocs.org/projects/asterix/badge/?version=latest)](http://asterix.readthedocs.io/en/latest/)
[![Upload Python Package](https://github.com/CroatiaControlLtd/asterix/actions/workflows/python-publish.yml/badge.svg)](https://github.com/CroatiaControlLtd/asterix/actions/workflows/python-publish.yml)
[![PyPI](https://img.shields.io/pypi/v/asterix_decoder.svg)](https://pypi.python.org/pypi/asterix_decoder)

> [!IMPORTANT]
> **This is a fork of [CroatiaControlLtd/asterix](https://github.com/CroatiaControlLtd/asterix).**
> The original project has not been updated since December 2025 (last commit for 2.8.11 on 2025-12-03, last release v2.8.10). Its decoder had memory-safety problems that needed to be fixed, and they are fixed here. ASTERIX data usually comes from the network, so malformed or truncated packets made the original code read past its buffers, which can crash it.
>
> Fork versions are numbered `<upstream version>-ifsnop.<n>`. The current one is **2.8.11-ifsnop.4**, based on upstream 2.8.11 (`asterix -V` shows both).
>
> Changes in this fork (see [HISTORY](HISTORY)):
> - **Security fixes**: out-of-bounds reads on truncated or malformed records (item lengths, PCAP and ORADIS headers), a stack overflow with `-LF` filters on BDS items, memory leaks with ORADIS input, invalid pointer casts, and string overflows in file naming. Found with AddressSanitizer/UBSan and fuzzing.
> - **Source IP** of each record in the TXT, JSON and XML outputs (`srcip`), taken from UDP multicast input and GPS files.
> - **New `-jx` / `--json-hex` output**: compact one-line-per-record JSON made to change a value and write it back to ASTERIX at minimal cost:
>   - each record has `fspec` (the FSPEC bytes: while a byte ends in 1 the next one belongs to it) and `hexdata` (the data items after it), so the record is `fspec + hexdata`;
>   - each field is `{"val":..., "hex":"...", "mask":"...", "pos":N}`: `pos` is the byte offset of `hex` in `hexdata` and `mask` (present when the field does not fill its hex bytes) the field's bits, so a field is rewritten with `hexdata[pos..] = (hex & ~mask) | (value & mask)`, without knowing the item structure;
>   - keys are unique in every object, so standard JSON parsers keep all fields: FX bits are numbered by extent (`FX0`, `FX1`, ...) and other repeated names get a number (`spare`, `spare1`).
>
>   Edits must keep the record length. FX bits, UAP selectors (e.g. CAT001 I020 TYP) and the BDS register number change how the rest is decoded.
> - **Record header with the FSPEC apart**: `-j`, `-jh`, `-je` and the default text output print the FSPEC bytes separately (`"fspec"` in JSON, a `Fspec:` line in text), and `hexdata` / `HexData:` holds only the data items after the FSPEC (same as `-jx`). **This differs from the original**, where hexdata was the whole record: use `fspec + hexdata` to get it. XML (`-x`, `-xh`) keeps the whole record.
> - **`-h`** documents the default text output (used when no output format option is given).
> - **Faster decoding, same output**: `-jx` is about 8 times faster (a 51 MB GPS file takes 8.5 s instead of 68 s) and the other output formats 2 to 2.6 times faster. The output is byte for byte the same as before.
> - **Build**: `make` now rebuilds the objects that include a header when it changes (the original makefiles never did). Run `make clean` once after updating.
>
> The `asterix_decoder` package on PyPI and the badges above belong to the original project. The PyPI package does not include these changes.

Asterix is Python module and standalone application used for reading and parsing of EUROCONTROL ASTERIX protocol data from stdin, file or network multicast stream.

ASTERIX stands for All Purpose STructured EUROCONTROL SuRveillance Information EXchange.
It is an ATM Surveillance Data Binary Messaging Format which allows transmission of harmonised information between any surveillance and automation system.
ASTERIX defines the structure of the data to be exchanged over a communication medium, from the encoding of every bit of information up to the organisation of the data within a block of data - without any loss of information during the whole process.
More about ASTERIX protocol you can find here: http://www.eurocontrol.int/services/asterix

Asterix application was developed by Croatia Control Ltd.

This program comes with ABSOLUTELY NO WARRANTY. This is free software, and you are welcome to redistribute it under certain conditions. See COPYING file for details.

It can be installed in Linux, Windows or MAC iOS environments. Windows is supported only if you use cygwin tools (make and gcc) for compilation.

To install python module run:
```console
pip install asterix_decoder
```

To compile python module from source run:
```console
python setup.py install
```

Then include it in python as:
```console
import asterix
```

To install executable (c/c++) version run:
```console
cd asterix/src
make install
```

or if you have CMake installed, run:
```console
cmake .
make
```

Then run it from asterix/install folder:
```console
./asterix
```

If you are using Windows install it through Cygwin terminal.
```console
Install following cygwin packages:
    binutils
    cygwin-devel
    gcc-core
    gcc-g++
    make
    libexpat-devel
```

For asterix specification handling, see [asterix-specs-converter](./asterix-specs-converter/README.md#asterix-specifications) directory.

For more information about the tool or Asterix protocol feel free to contact the author:
<html>
<div class="LI-profile-badge"  data-version="v1" data-size="medium" data-locale="en_US" data-type="horizontal" data-theme="light" data-vanity="damirsalantic"><a class="LI-simple-link" href='https://hr.linkedin.com/in/damirsalantic?trk=profile-badge'>Damir Salantic</a></div>
</html>

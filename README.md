# tapemgr

Create, scan, and extract AWS tape images from the host side of a Hercules
system. It exists to move datasets between a Linux or macOS host and MVS 3.8j
without losing record boundaries: the tape carries the BDW/RDW/SDW bytes that
MVS wrote, and `tapemgr` reads them back exactly as it would on the guest.

It handles standard-label (SL) tapes with `RECFM=F`, `FB`, `V`, `VB`, `VS`,
and `VBS`, including spanned records that cross block boundaries, and converts
text between UTF-8 and EBCDIC code pages 037, 273, 277, 285, 500, and 1047.

## Build

```
make          # builds ./tapemgr
make test     # runs the test suite (needs bash and python3)
```

A C++17 compiler is all that is required. The one external dependency,
[nlohmann/json](https://github.com/nlohmann/json) 3.12.0, is vendored under
`third_party/`.

## Getting a dataset off MVS

On MVS, copy the dataset to a standard-label tape. IEBGENER with `DCB=*.SYSUT1`
carries the record format through unchanged:

```
//COPY    EXEC PGM=IEBGENER
//SYSPRINT DD SYSOUT=*
//SYSUT1   DD DSN=SYSO.SMF.DATA(0),DISP=SHR
//SYSUT2   DD DSN=SMF.DUMP,UNIT=TAPE,VOL=SER=SMF001,
//            LABEL=(1,SL),DISP=(NEW,KEEP),
//            DCB=*.SYSUT1
//SYSIN    DD DUMMY
```

On the host, let `init` read the HDR2 labels and write a config:

```
$ ./tapemgr init -o smf.json smf001.aws
$ cat smf.json
{
    "files": [
        {
            "binary": false,
            "block_attribute": "R",
            "block_count": 10,
            "block_size": 4096,
            "creation_date": "026247",
            "dataset_name": "SMF.DUMP",
            "dataset_org": "PS",
            "expiration_date": "026277",
            "file_position": 264,
            "local_file": "",
            "record_format": "VBS",
            "record_length": 32760
        }
    ],
    "owner_code": "TAPEOWNER",
    "volume_serial": "SMF001"
}
```

Fill in `local_file` and, for anything that is not text, set `binary` to
`true`. Then extract:

```
$ ./tapemgr extract -c smf.json smf001.aws
```

How records land on disk depends on `binary`:

| `binary` | RECFM | On disk |
|----------|-------|---------|
| `false`  | any   | one line per record, EBCDIC converted to UTF-8, trailing spaces trimmed |
| `true`   | F, FB | records concatenated, `record_length` bytes each |
| `true`   | V, VB, VS, VBS | each record preceded by a 4-byte RDW (2-byte big-endian length including the RDW, then two zero bytes); spanned records are reassembled |

Text mode accepts an optional `"codepage"`: `CP037` (default, US and
Canada), `CP273` (Germany and Austria), `CP277` (Denmark and Norway), `CP285`
(United Kingdom), `CP500` (international Latin-1), or `CP1047` (z/OS Unix and
the C compiler). Each table is generated from a 256-entry mapping file under
`src/`, checked against ICU's tables, and verified exhaustively by
`tests/codepage_check.cpp`.

## Sending a dataset to MVS

Write a config by hand (the same fields `init` produces; `block_attribute`,
`dataset_org`, dates, and positions are optional) and run `create`:

```
$ cat files.json
{
  "volume_serial": "VOL001",
  "owner_code": "MYOWNER",
  "files": [
    {
      "dataset_name": "MY.ACCOUNTS",
      "local_file": "accounts.txt",
      "record_format": "FB",
      "record_length": 80,
      "block_size": 3200
    }
  ]
}
$ ./tapemgr create --volser=VOL001 -o vol001.aws -c files.json
```

Input files are read the same way extraction writes them: text files one line
per record (padded or truncated to `record_length` for F/FB), binary fixed
files in `record_length` chunks, binary variable files with a 4-byte RDW in
front of each record. The tape gets VOL1, HDR1, HDR2, EOF1, and EOF2 labels
so MVS can read it with `LABEL=(n,SL)` and no DCB overrides.

`create` also writes `RESTORE.JCL` in the current directory: one IEFBR14
delete step and one IEBGENER copy step per file, restoring each dataset from
the tape to DASD. The tape volser comes from `--volser`. The DASD volume and
unit default to `PUB001` and `3350`; set `default_volser` and `default_unit`
at the top level of the config to change them for the whole tape, or
`target_volser` and `target_unit` on a file entry to place that one dataset
elsewhere. The HDR2 job/step field on the tape reads `BRAZIL/TAPEMGR`.

## Scan

```
$ ./tapemgr scan vol001.aws
Tape: vol001.aws
Volume Serial: VOL001
Files: 1

Dataset: MY.ACCOUNTS
  Format: FB
  Record Length: 80
  Block Size: 3200
  Blocks: 4
  Created: 026247
  Expires: 026277
```

Add `-v` (up to three times) for label fields and per-block detail.

## AWS format in brief

Every block in the file is a 6-byte little-endian header followed by the
block's data: 2 bytes for this block's length, 2 bytes for the previous
block's length, and 2 flag bytes. A tape mark is a header with zero length
and bit `0x40` set in the first flag byte. An SL tape reads as VOL1, HDR1,
HDR2, tape mark, data blocks, tape mark, EOF1, EOF2, tape mark; a second
consecutive tape mark ends the volume.

Inside a data block for variable formats, the first 4 bytes are the BDW and
each record starts with a 4-byte RDW (or SDW when spanned). The length fields
are big-endian and include the descriptor. For spanned records the low two
bits of the third SDW byte are `00` complete, `01` first segment, `11`
middle, `10` last.

## Status

The test suite round-trips each record format through `create` and
`extract`. The tool has been used in production to load tapes into MVS 3.8j,
but extraction of a `VBS` tape written by MVS itself has not yet been
exercised. Reports of either outcome are welcome.

## Adding a code page

The EBCDIC tables under `src/` are generated, not hand-written. To add one:

1. Write `src/tr_utf8_cpNNN.txt`, one line per EBCDIC byte in the form
   `UUUU;ddd;NAME;` (Unicode code point in hex, EBCDIC byte in decimal,
   character name). ICU's `.ucm` files are a good source; only the
   round-trip (`|0`) entries belong in it, and there must be exactly 256.
2. Build the `integers` DFA builder from
   [brazilofmux/utf](https://github.com/brazilofmux/utf) (`gen/integers.cpp`
   with `smutil.cpp` and `ConvertUTF.cpp`) and run
   `tools/mktables.py --integers <path> NNN`.
3. Add `CPNNN` to the enum in `src/ebcdic_converter.h`, a registry entry in
   `src/ebcdic_converter.cpp`, the two `"CPNNN"` parse sites and the valid
   list in `src/tapemgr.cpp`, and the new `.cpp` to the Makefile.

`make test` then checks every byte and every code point of every page in
both directions.

## License

MIT. See `LICENSE`.

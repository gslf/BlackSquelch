# Protocol

BlackSquelch supports 8 or 16 tones at 25 symbols/s. RX detects the mode from
training. These packet formats are specific to BlackSquelch.

| Mode | Tone frequency | Bits per symbol |
| --- | --- | ---: |
| 8 tones | `900 + 200*T` Hz, T=0…7 | 3 |
| 16 tones | `800 + 100*T` Hz, T=0…15 | 4 |

Audio is 48 kHz mono PCM16. Each symbol lasts 1,920 samples.
Phase is continuous. Packet edges have 5 ms ramps.
Numbers and character codes are sent most-significant bit first.

## Physical packet

A packet contains training, a protected header and a protected body.

Training uses these 32 tone indices. For 8 tones, take each index modulo 8:

```text
0 15 3 12 6 9 5 10 1 14 7 8 2 13 4 11
5 12 1 10 7 14 3 8 4 15 6 13 0 11 2 9
```

The five-byte header contains:

| Bytes | Value |
| --- | --- |
| 1 | Physical version: 1 |
| 2 | Logical frame length in bits, including its 64-bit prefix |
| 2 | CRC-16 of the first three header bytes |

Length must be byte-aligned and between 136 and 3,208 bits.
The header is encoded separately into 96 interleaved bits.

The body contains logical bits `[64, length)`. The fixed logical prefix is omitted
on air and restored before parsing. The body includes its own CRC-16.

Before encoding the body, XOR its bits with this sequence:

```text
state = 0x1FF
output = state & 1
feedback = (state ^ (state >> 4)) & 1
state = (state >> 1) | (feedback << 8)
```

Reset this state for each body. The header is not whitened.

## Error correction

Header and body use separate convolutional encoders:

- Rate 1/2, constraint length 7.
- Start in state zero; append six zero bits.
- For each bit: `state = ((state << 1) | bit) & 127`.
- Output parity of `state & 0171`, then `state & 0133` (octal).
- No output inversion. Soft Viterbi decoding ends in state zero.

Input `1` followed by the six tail zeros produces `11010011111011` before padding.

For B input bits, pad the coded bits with zeros to:

```text
C(B) = 48 * ceil(2*(B+6)/48)
```

Interleave in 16 rows. With K=C/16 columns, coded index i is sent at
`(i % K)*16 + floor(i/K)`. Padding is ignored after deinterleaving.
Positive soft values favor one.

Group coded bits in threes or fours. Tone T represents Gray code `T ^ (T >> 1)`.
Before data symbols 16, 32, … insert pilot tones 0 and M-1. Header and body share
this count. There are no trailing pilots. Pilots support timing and frequency
tracking and do not enter error correction or CRC checks.

## Logical frames

All logical frames start with the fixed prefix `AA AA AA AA 1A CF FC 1D`.
This prefix is reconstructed, not sent inside the physical body.

| Field | Bits |
| --- | ---: |
| Callsign | 36: six character codes |
| Text length or type marker | 16 |
| Content | Depends on type |
| Zero padding | Aligns the body to a byte |
| CRC-16 | 16 |

CRC-16 covers the body from callsign through padding. It uses polynomial `0x1021`,
initial value `0xFFFF`, no reflection and final XOR zero.
`123456789` checks to `0x29B1`. CRC cannot override an invalid field.

### Text

Length is 1–512. Each character uses six bits:

| Codes | Characters in order |
| --- | --- |
| 0–25 | `ABCDEFGHIJKLMNOPQRSTUVWXYZ` |
| 26–35 | `0123456789` |
| 36 | Space |
| 37–46 | `.,?!:;-/()` |
| 47–63 | Invalid |

Callsigns use codes 0–35 only. Lowercase is normalized before sending.
For N text characters, padding is `(8 - (52 + 6*N) % 8) % 8` bits.

Golden logical frame for `IU2ABC / CIAO` (160 bits, CRC `AC84`):

```text
AA AA AA AA 1A CF FC 1D 21 47 00 04 20 00 40 88 00 E0 AC 84
```

### Files

Marker `0xFFFF` selects file version 2. The content starts with this header:

| Offset | Bytes | Field |
| --- | ---: | --- |
| 0 | 1 | File version: 2 |
| 1 | 4 | Transfer ID |
| 5 | 4 | Total size: 0–1,048,576 bytes |
| 9 | 4 | Whole-file CRC-32 |
| 13 | 2 | Zero-based block index |
| 15 | 1 | Filename length: 1–96 bytes |
| 16 | 2 | Data length |
| 18 | N | Filename |
| 18+N | L | Unchanged file bytes |

Block count is `max(1, ceil(size/64))`. Block I starts at byte `64*I` and contains
`min(64, size - 64*I)` bytes. Empty files use one empty block. Padding is four bits.
Each block repeats the metadata, so reception can start after a lost block.

Filenames allow ASCII letters, digits and underscore. Dot and hyphen are allowed
after the first character. Paths are rejected.

CRC-32 uses reflected polynomial `0xEDB88320` and initial/final XOR `0xFFFFFFFF`.
`123456789` checks to `0xCBF43926`; an empty file checks to zero.

Only verified blocks enter assembly. Callsign, ID and metadata must agree.
Duplicates must contain the same bytes. Saving requires every block and a valid
whole-file checksum. A failed whole-file checksum clears the assembly.

### Repair reports

Marker `0xFFFE` selects this 45-byte content:

| Offset | Bytes | Field |
| --- | ---: | --- |
| 0 | 1 | Type: 1=request, 2=complete receipt |
| 1 | 6 | Target sender callsign, uppercase ASCII |
| 7 | 4 | Transfer ID |
| 11 | 2 | Base block index: multiple of 256, below 16,384 |
| 13 | 32 | Missing-block bitmap |

Bit 7 of byte 0 requests base; bit 6 requests base+1, and so on.
Requests must set a bit. Complete receipts require base zero and an empty bitmap.
Add four padding bits and CRC-16. A logical report is 496 bits.

Only matching target/ID reports are applied. Any out-of-range requested block
rejects the whole report. Valid requests merge; complete receipts clear the queue.
Reports use the same physical protection as data. PTT remains manual.
Checksums do not authenticate a station.

## Duration

For logical length L and tone count M:

```text
D = (96 + C(L-64)) / log2(M)
S = 32 + D + 2*floor((D-1)/16)
audio seconds = S / 25
```

`CIAO` takes 6.24 s with 8 tones or 5.04 s with 16.
Add countdown, device delays, PTT turns and repairs.
See [useful file speeds](../README.md#speed) for application-level examples.

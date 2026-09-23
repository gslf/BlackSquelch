# Wire protocol

All numeric values and character codes are transmitted most-significant bit first.
There is no bit stuffing, escape sequence, end marker, or implicit terminator.
One message is one frame. The callsign occupies exactly 36 bits, with no length
field. Both endpoints use the format specified below.

| Field | Bits | Valid values |
| --- | ---: | --- |
| Preamble | 32 | `10101010101010101010101010101010` |
| Synchronization | 32 | `0x1ACFFC1D`, exact match |
| Callsign | 36 | Exactly six characters, alphabet codes 0–35 |
| Text length | 16 | 1–512 |
| Text | 6 × N | Alphabet codes 0–46 |
| Padding | P | All zeros |
| CRC | 16 | CRC-16/CCITT-FALSE, high byte first |

The body begins with the first six-bit callsign character. Its fields are concatenated without
intermediate alignment, including the 16-bit text length. With
`B = 52 + 6 * N`, padding is `P = (8 - B % 8) % 8`, taking values 0, 2, 4,
or 6. The CRC covers the packed body including padding, excluding preamble,
synchronization, and the CRC itself.

CRC parameters: polynomial `0x1021`, initial value `0xFFFF`, no input or output
reflection, final XOR zero. The byte string `123456789` checks to `0x29B1`.
This CRC check string is ASCII bytes, not six-bit encoded text.

## Rate acquisition

The same complete frame, including preamble, is transmitted at 10, 25, or 50 bit/s.
Tone frequencies stay at 1200/2200 Hz and PCM stays at 48 kHz. One bit occupies
4800, 1920, or 960 samples respectively. No rate bits are inserted into the frame.

The receiver tests all three symbol clocks against the 32 alternating preamble
bits. A matching preamble locks one rate; the following synchronization word must
then match exactly. The acquired clock feeds the rest of that frame, regardless
of payload bit patterns. Completion, format failure, or sustained signal loss
returns the receiver to automatic rate search. The receiver's locally selected
transmit rate is irrelevant to acquisition.

The preamble lasts 3.20, 1.28, or 0.64 seconds. The silent two-second operator
countdown is separate and is not used for acquisition. Rate detection is based on
observed timing rather than message content, a shared setting, or an ACK exchange.

## Alphabet

| Codes | Characters in order |
| --- | --- |
| 0–25 | `ABCDEFGHIJKLMNOPQRSTUVWXYZ` |
| 26–35 | `0123456789` |
| 36 | Space |
| 37–46 | `.,?!:;-/()` |
| 47–63 | Reserved and invalid |

`A=000000`, `B=000001`, `Z=011001`, `0=011010`, `9=100011`, space=`100100`,
period=`100101`. The log names code 36 `SPACE`. Received controls cannot exist in
this alphabet. Reserved codes are reported numerically through their original bit
group, never used as terminal characters.

ASCII lowercase is normalized before validation. Unicode, accents, tabs, newlines,
and other bytes outside the alphabet block sending. Callsign validity is stricter
than text validity; punctuation and spaces cannot appear in a callsign.

## Golden vector

Callsign `IU2ABC`, text `CIAO`:

```text
Preamble:       AA AA AA AA
Synchronization:1A CF FC 1D
Body:           21 47 00 04 20 00 40 88 00 E0
CRC:            AC 84
Full frame:     AA AA AA AA 1A CF FC 1D 21 47 00 04 20 00 40 88 00 E0 AC 84
```

```text
Call:        001000 010100 011100 000000 000001 000010
Text length: 0000000000000100
Text:        000010 001000 000000 001110
Padding:     0000
CRC:         1010110010000100
```

There are 160 bits. The text length starts at bit offset 100 from the first
preamble bit, so it straddles byte boundaries. Text starts at offset 116. The
body CRC was independently cross-checked with Python's `binascii.crc_hqx` using
initial value 65535; the automated C test stores the literal full frame.

## Outcomes and bounded parsing

The parser emits progress after every bit and completion after every field or
character. A partial group uses dashes for missing bits; missing bits never become
zero. Caller-owned event snapshots contain partial bits, frame position, confidence,
character index, and cumulative provisional previews.

A bad text length, invalid callsign character, or synchronization
mismatch immediately abandons the candidate. Reserved text codes retain their
position (the preview uses `#` as an explicitly invalid placeholder), allowing the
remaining fields and CRC to be inspected. Nonzero padding invalidates the frame.
A matching CRC cannot override a semantic error. Only a fully valid frame emits
`BS_VERIFIED`; invalid and interrupted previews remain unverified.

Sync is recognized only following an alternating preamble candidate. It is never
searched inside an acquired body. The parser owns fixed-size storage: 7 bytes for
a callsign, 513 for text, and no length-dependent allocations. The largest frame
contains 3208 bits. A reset clears callsign, text, byte assembly, and CRC state.

Duration is `(132 + 6 * N + P) / R` seconds, where `R` is 10, 25, or 50 bit/s,
excluding the two-second operator countdown. A 100-character message has 736 bits
and takes 73.60, 29.44, or 14.72 seconds. All rate profiles pack exactly the same
frame bits and CRC.

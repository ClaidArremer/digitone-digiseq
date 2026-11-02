# Changelog: DIGISEQ for Digitone

**This is the Digitone mk1 / Digitone Keys version** (`digiseq`). For the Digitakt, see
[DIGISEQ for Digitakt](https://github.com/ClaidArremer/digitakt-digiseq).

Each version's files are in `elemods/` (`digiseq-<version>-os1.44.elemod` and `-os1.43`); the newest is there,
older ones are in the history of this repo.

The Digitakt version has gone on from 0.2 with things this one does not have: the ARP type (the Digitone has its
own arpeggiator) and live recording of what a type plays. Its changelog lists them.

## 0.2 (2026-10-26)

- PRINT puts each printed note on its step with no trig condition, so a trig left there from before can no
  longer shift it (micro timing) or mute it on some loops (a condition such as 1:2).
- Steps a type plays ignore the track's own trigs' conditions and probabilities.

## 0.1 (2026-10-25)

- The first version: SNAKE (from DIGISNAKE 1.3) and 185. Run in an emulator (digiemu), then played on a
  Digitone mk1 (OS 1.44) with PUMP 2.10, DIRECT JUMP 1.5, DIGICOMPLEX, DH-101, digihealth and LØST LVLS.

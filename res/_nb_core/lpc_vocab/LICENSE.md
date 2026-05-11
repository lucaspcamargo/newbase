# LPC Vocabulary — Provenance and Attribution

The `.rlpcvocab` files in this directory were generated from TalkiePCM vocabulary
headers (https://github.com/ArminJo/TalkiePCM) using
`scripts/generate_talkie_vocab.py`. The headers were prepared by Armin Joachimsmeyer
(2018–2019) from earlier work by Peter Knight's
[Talkie](https://github.com/going-digital/Talkie) library (2011). The underlying LPC
bitstream data originates from various hardware ROMs and other sources as described
below.

---

## Special.rlpcvocab

Silence/pause utility entries. No third-party ROM source.

---

## US_Large.rlpcvocab

Derived from Texas Instruments speech ROM chips **VM61002, VM61003, VM61004, and
VM61005**. Large general-purpose US English vocabulary, male voice. Originally shipped
with TI speech products in the early 1980s.

---

## US_Clock.rlpcvocab

Clock and time-reading vocabulary, female American voice. TI speech ROM origin.

---

## US_Acorn.rlpcvocab

Derived from the **Acorn Computers Speech Synthesizer** add-on (1983). Male Received
Pronunciation (RP) English accent, voiced by **Kenneth Kendall** (BBC newsreader).

---

## US_TI99.rlpcvocab

Derived from the **Texas Instruments TI-99/4A Speech System** add-on cartridge (1979).
Deep male Southern USA accent; one of the largest single-chip vocabularies at 32 KB.

---

## AstroBlaster.rlpcvocab

LPC data extracted from the **Astro Blaster** arcade game ROM by **Richard Broadhurst**.

---

## Soundbites.rlpcvocab

Phrases from **SPROW's** custom BBC Micro phrase ROM: "What is thy bidding?",
"Hasta la vista, baby", "One small step for man…", and "Hmmm, beer".

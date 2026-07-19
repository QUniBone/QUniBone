# Reference text

`EK-DELQA-UG-002.txt` is the DELQA User's Guide, OCR'd so it can be grepped.
The scan is at
`treasures.scss.tcd.ie/hardware/TCD-SCSS-T.20141120.008/EK-DELQA-UG-002.pdf`
and carries no text layer; `ocrmypdf --force-ocr` followed by `pdftotext
-layout` produces this. Read it here rather than paging through the images —
the register and status-word tables come through cleanly, and answering a
question by grep beats answering it by inference.

Section 3 holds the programming model. The parts that decide DELQA emulation
behaviour:

- 3.3.2.1 CSR bits, including the CSR08/CSR09 table that names the four
  loopback modes. The prose in 3.6.5 contradicts that table and is wrong.
- 3.4.3.1 the flag word, 3.4.3.5 the transmit and receive status words.
- 3.6.5 loopback, which states that internal loopback carries six-byte
  packets and nothing else, and that external loopback needs a connector.

DEC's CZQNA diagnostic listings are on bitsavers under
`pdf/dec/pdp11/microfiche/Diagnostic_Program_Listings/Listings/`, revisions A
through E. Those are the DEQNA-only versions; the DEQNA/DELQA/DESQA rewrite
that XXDP 2.5 carries as `ZQNAJ0.BIC` was never fiched, so it has to be read
by disassembling the binary itself.

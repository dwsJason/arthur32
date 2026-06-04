* main.s - normal code segment for the bigdata round-trip test
* Assembled by merlin32 as part of bigdata.link (multi-segment OMF).

            rel
            mx %00

Start       clc
            xce
            rep #$30
            mx %00

            lda #$1234
            sta $00
            rtl

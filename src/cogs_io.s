;
; cogs_io.s -- CoGS register primitives (65C02, cc65 calling convention).
;
; Adapted from a2chat's src/cogs_io.s, which has run on a real card.
;
; The card exposes sixteen registers at $C080 + slot*16 and a framed
; command protocol (CoGS Technical Reference, chapters 2 and 3):
;
;   +0 DATA     write: next byte to the card / read: pop the next byte
;   +1 STATUS   bit 7 RX_AVAIL, bit 6 TX_READY, bit 4 LINK_UP, bit 3 ERROR
;   +4 CONTROL  write-only pulses: $08 flush host->card and reset the
;               parser, $04 flush card->host, $01 soft reset (never used:
;               it would also abort a network disk ProDOS may own)
;   +5 ID       always $A2
;   +6 VERSION  register-map generation, nonzero
;   +A SYNC     increments when a parser reset completes
;
; Rules this file keeps (reference 2.5 and chapter 8):
;   - every DATA read is gated on STATUS RX_AVAIL, because an empty FIFO
;     reads $00, which is indistinguishable from a real zero byte;
;   - every DATA write is gated on STATUS TX_READY;
;   - registers are reached as abs,X with X = slot*16, so the indexed-store
;     dummy read of DATA is the one case the card compensates for;
;   - every wait is bounded, so a missing card cannot hang the Apple.

        .export         _cogs_set_slot
        .export         _cogs_probe
        .export         _cogs_status
        .export         _cogs_get
        .export         _cogs_getn
        .export         _cogs_skip
        .export         _cogs_putn
        .export         _cogs_resync
        .export         _cogs_irq_off

        .import         popax
        .importzp       ptr1, ptr2, tmp1, tmp2

REG_DATA        = $C080
REG_STATUS      = $C081
REG_CONTROL     = $C084
REG_ID          = $C085
REG_VERSION     = $C086
REG_SYNC        = $C08A

ST_TX_READY     = $40

        .bss
slotx:  .res    1               ; slot * 16
putmid: .res    1               ; middle byte of the TX wait counter

        .code

; void __fastcall__ cogs_set_slot(unsigned char slot)
_cogs_set_slot:
        asl
        asl
        asl
        asl
        sta     slotx
        rts

; unsigned char __fastcall__ cogs_probe(unsigned char slot)
; 1 if a CoGS card answers in the slot. Reads only ID, VERSION and STATUS,
; never DATA. The caller must have ruled out a Disk II: its $C0nX switches
; move the head.
;
; The reference's test is one read each: ID $A2, VERSION nonzero, STATUS
; not $FF. On a //e that is not enough. An undecoded I/O address returns
; the floating bus, the byte the video circuitry is fetching from screen
; memory at that moment, and with the right text on screen it reads $A2:
; Virtual ][ showed a CoGS in slot 2 that way. So ID must read $A2 32
; times running, interleaved with VERSION, which must hold one value from
; 1 to 15 (a register-map generation: $04 in the reference, $05 on 1.42).
; About 25 cycles a pass, so the reads span many video fetches.
_cogs_probe:
        asl
        asl
        asl
        asl
        tax
        lda     REG_VERSION,x
        beq     @no
        cmp     #$10
        bcs     @no
        sta     tmp1            ; the VERSION every pass must see
        ldy     #32
@pass:  lda     REG_ID,x
        cmp     #$A2
        bne     @no
        lda     REG_VERSION,x
        cmp     tmp1
        bne     @no
        lda     REG_STATUS,x
        cmp     #$FF
        beq     @no
        dey
        bne     @pass
        lda     #1
        ldx     #0
        rts
@no:    lda     #0
        tax
        rts

; unsigned char cogs_status(void) -- the STATUS register. No side effects.
_cogs_status:
        ldx     slotx
        lda     REG_STATUS,x
        ldx     #0
        rts

; int cogs_get(void)
; Wait (bounded) for one byte and pop it. 0..255, or -1 on timeout.
_cogs_get:
        ldx     slotx
        lda     #0
        sta     tmp1
        sta     tmp2
        ldy     #$04            ; 4 * 65536 polls, about 3 s at 1 MHz
@w:     lda     REG_STATUS,x
        bmi     @rd
        inc     tmp1
        bne     @w
        inc     tmp2
        bne     @w
        dey
        bne     @w
        lda     #$FF
        tax
        rts
@rd:    lda     REG_DATA,x
        ldx     #0
        rts

; unsigned char __fastcall__ cogs_getn(unsigned char *dst, unsigned char n)
; Pop n bytes into dst, each gated on RX_AVAIL. a2chat found that reading
; DATA back to back on the strength of the RXLO/RXHI count alone garbled
; replies on real hardware. 0 on success, 1 if a byte never arrived.
_cogs_getn:
        sta     tmp2            ; n
        jsr     popax
        sta     ptr1
        stx     ptr1+1
        ldx     slotx
        ldy     #0
        cpy     tmp2
        beq     @ok
@byte:  lda     REG_STATUS,x
        bpl     @slow
@rd:    lda     REG_DATA,x
        sta     (ptr1),y
        iny
        cpy     tmp2
        bne     @byte
@ok:    lda     #0
        tax
        rts
@slow:  lda     #0              ; bounded wait, about 1 s at 1 MHz
        sta     tmp1
        sta     putmid
@w:     lda     REG_STATUS,x
        bmi     @rd
        inc     tmp1
        bne     @w
        inc     putmid
        bne     @w
        lda     #1
        ldx     #0
        rts

; unsigned char __fastcall__ cogs_skip(unsigned int n)
; Pop and discard n bytes. 0 on success, 1 on timeout.
_cogs_skip:
        sta     ptr2
        stx     ptr2+1
@next:  lda     ptr2
        ora     ptr2+1
        beq     @ok
        jsr     _cogs_get
        cpx     #$FF
        beq     @to
        lda     ptr2
        bne     :+
        dec     ptr2+1
:       dec     ptr2
        bra     @next
@ok:    lda     #0
        tax
        rts
@to:    lda     #1
        ldx     #0
        rts

; put_x: wait (bounded) for TX_READY, then write tmp2. X = slot*16.
; Clobbers A, Y, tmp1, putmid. A = 0 ok, 1 timeout.
put_x:  lda     #0
        sta     tmp1
        sta     putmid
        ldy     #$04            ; about 3 s at 1 MHz
@w:     lda     REG_STATUS,x
        and     #ST_TX_READY
        bne     @wr
        inc     tmp1
        bne     @w
        inc     putmid
        bne     @w
        dey
        bne     @w
        lda     #1
        rts
@wr:    lda     tmp2
        sta     REG_DATA,x
        lda     #0
        rts

; unsigned char __fastcall__ cogs_putn(const unsigned char *src, unsigned int n)
; Write n bytes, each gated on TX_READY. 0 ok, 1 timeout.
_cogs_putn:
        sta     ptr2
        stx     ptr2+1
        jsr     popax
        sta     ptr1
        stx     ptr1+1
        ldx     slotx
@next:  lda     ptr2
        ora     ptr2+1
        beq     @ok
        lda     REG_STATUS,x    ; fast path: room for the byte already
        and     #ST_TX_READY
        beq     @slow
        lda     (ptr1)
        sta     REG_DATA,x
        bra     @adv
@slow:  lda     (ptr1)
        sta     tmp2
        jsr     put_x
        ldx     slotx
        cmp     #0
        bne     @to
@adv:   inc     ptr1
        bne     :+
        inc     ptr1+1
:       lda     ptr2
        bne     :+
        dec     ptr2+1
:       dec     ptr2
        bra     @next
@ok:    lda     #0
        tax
        rts
@to:    lda     #1
        ldx     #0
        rts

; void cogs_irq_off(void)
; Mask IRQs. One context owns the FIFO at a time (reference 8.5); an
; interrupt handler that popped DATA would steal bytes from our frames.
_cogs_irq_off:
        sei
        rts

; unsigned char cogs_resync(void)
; Flush host->card and reset the parser ($08), flush card->host ($04),
; then wait for SYNC to move. 0 ok, 1 SYNC never moved.
_cogs_resync:
        ldx     slotx
        lda     REG_SYNC,x
        sta     tmp2
        lda     #$08
        sta     REG_CONTROL,x
        lda     #$04
        sta     REG_CONTROL,x
        lda     #0
        sta     tmp1
        ldy     #0
@w:     lda     REG_SYNC,x
        cmp     tmp2
        bne     @ok
        inc     tmp1
        bne     @w
        iny
        bne     @w
        lda     #1
        ldx     #0
        rts
@ok:    lda     #0
        tax
        rts

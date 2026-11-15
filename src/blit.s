;
; blit.s -- 80-column screen primitives (from Newsiie) (65C02, cc65 ABI).
;
; Text page 1 is split between the two banks: even screen columns live in
; auxiliary memory and odd columns in main, both at $0400-$07FF. With 80STORE
; on, TXTPAGE2 sends page-1 writes to aux and TXTPAGE1 sends them to main;
; neither changes which page is displayed.
;
; Nothing here runs with interrupts in mind: ProDOS 8 on a stock //e takes
; none, and the writes are plain stores, never RAMRD.

        .export         _scr_init
        .export         _scr_done
        .export         _scr_blit
        .export         _lc_ram
        .import         _linebuf
        .importzp       ptr1

CLR80STORE      = $C000
SET80STORE      = $C001
CLR80COL        = $C00C
SET80COL        = $C00D
CLRALTCHAR      = $C00E
SETALTCHAR      = $C00F
TXTSET          = $C051
TXTPAGE1        = $C054
TXTPAGE2        = $C055
LCRAM           = $C080         ; read LC RAM bank 2, no write
LCROM           = $C082         ; read ROM, no write

ROM_INIT        = $FB2F         ; text mode, full window
ROM_SETKBD      = $FE89
ROM_SETVID      = $FE93
ROM_HOME        = $FC58

        .rodata

; Base address of each of the 24 text rows.
rowlo:  .byte   $00,$80,$00,$80,$00,$80,$00,$80
        .byte   $28,$A8,$28,$A8,$28,$A8,$28,$A8
        .byte   $50,$D0,$50,$D0,$50,$D0,$50,$D0
rowhi:  .byte   $04,$04,$05,$05,$06,$06,$07,$07
        .byte   $04,$04,$05,$05,$06,$06,$07,$07
        .byte   $04,$04,$05,$05,$06,$06,$07,$07

        .code

; void scr_init(void) -- 80 columns, text page 1, alternate character set
; (MouseText and inverse lowercase).
_scr_init:
        sta     TXTSET
        sta     SET80COL
        sta     SET80STORE
        sta     TXTPAGE1
        sta     SETALTCHAR
        rts

; void scr_done(void) -- hand a clean 40-column screen back to ProDOS.
;
; cc65's startup leaves language-card RAM readable at $D000-$FFFF while the
; program runs (bit $C080) and only restores the ROM in its exit code, so
; the monitor routines have to be switched in around these calls. Without
; that they run whatever ProDOS keeps there and land in the monitor.
_scr_done:
        sta     TXTPAGE1
        sta     CLR80STORE
        sta     CLR80COL
        sta     CLRALTCHAR
        bit     LCROM
        jsr     ROM_INIT
        jsr     ROM_SETVID
        jsr     ROM_SETKBD
        jsr     ROM_HOME
        bit     LCRAM
        rts

; void lc_ram(void) -- read language-card RAM, bank 2, as cc65's startup
; leaves it. Called after ProDOS by main code whose caller runs from the
; language card (save.c), in case ProDOS did not restore it.
_lc_ram:
        bit     LCRAM
        rts

; void __fastcall__ scr_blit(unsigned char row) -- copy linebuf to a row.
_scr_blit:
        tax
        lda     rowlo,x
        sta     ptr1
        lda     rowhi,x
        sta     ptr1+1

        sta     TXTPAGE2        ; even columns: auxiliary
        ldy     #0
        ldx     #0
@even:  lda     _linebuf,x
        sta     (ptr1),y
        inx
        inx
        iny
        cpy     #40
        bne     @even

        sta     TXTPAGE1        ; odd columns: main
        ldy     #0
        ldx     #1
@odd:   lda     _linebuf,x
        sta     (ptr1),y
        inx
        inx
        iny
        cpy     #40
        bne     @odd
        rts

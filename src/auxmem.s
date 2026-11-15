;
; auxmem.s -- copying between main and auxiliary memory (65C02, cc65).
;
; Writing to aux is easy: RAMWRT sends stores to $0200-$BFFF into aux while
; instructions are still fetched from main. Reading is not: RAMRD also
; redirects instruction fetches, so code at $2000 that switched it on would
; fetch its next instruction from aux and die. Pages $00 and $01 follow
; ALTZP instead of RAMRD, so the read loop runs from a copy in the bottom of
; the stack page, $0100, which the 6502 stack (growing down from $01FF)
; never reaches in this program. aux_init() puts it there.
;
; Interrupts: none are taken under ProDOS 8 on a stock //e, and the card
; code masks them; an interrupt with RAMRD on would run its handler from
; aux. Kept in mind, not defended against.

        .export         _aux_init
        .export         _aux_put
        .export         _aux_get
        .import         popax
        .importzp       ptr1, ptr2, ptr3

RAMRDOFF        = $C002
RAMRDON         = $C003
RAMWRTOFF       = $C004
RAMWRTON        = $C005

STUB            = $0100

        .rodata

; The read loop, assembled to run at $0100. ptr1 = aux source, ptr2 = main
; destination, ptr3 = count. About 20 cycles a byte.
stub_image:
        .org    STUB
getstub:
        ldy     #0
        ldx     ptr3+1          ; whole pages first
        beq     @part
@page:  sta     RAMRDON
        lda     (ptr1),y
        sta     RAMRDOFF
        sta     (ptr2),y
        iny
        bne     @page
        inc     ptr1+1
        inc     ptr2+1
        dex
        bne     @page
@part:  ldx     ptr3
        beq     @done
@byte:  sta     RAMRDON
        lda     (ptr1),y
        sta     RAMRDOFF
        sta     (ptr2),y
        iny
        dex
        bne     @byte
@done:  rts
stub_end:
        .reloc
STUB_LEN = stub_end - getstub

        .code

; void aux_init(void) -- copy the read loop to $0100.
_aux_init:
        ldx     #STUB_LEN - 1
@cp:    lda     stub_image,x
        sta     STUB,x
        dex
        bpl     @cp
        rts

; void __fastcall__ aux_put(unsigned int aux_dst, const void *src, unsigned int n)
_aux_put:
        sta     ptr3
        stx     ptr3+1
        jsr     popax           ; src (main)
        sta     ptr1
        stx     ptr1+1
        jsr     popax           ; dst (aux)
        sta     ptr2
        stx     ptr2+1
        sta     RAMWRTON        ; stores go to aux, fetches stay in main
        ldy     #0
        ldx     ptr3+1
        beq     @part
@page:  lda     (ptr1),y
        sta     (ptr2),y
        iny
        bne     @page
        inc     ptr1+1
        inc     ptr2+1
        dex
        bne     @page
@part:  ldx     ptr3
        beq     @done
@byte:  lda     (ptr1),y
        sta     (ptr2),y
        iny
        dex
        bne     @byte
@done:  sta     RAMWRTOFF
        rts

; void __fastcall__ aux_get(void *dst, unsigned int aux_src, unsigned int n)
_aux_get:
        sta     ptr3
        stx     ptr3+1
        jsr     popax           ; src (aux)
        sta     ptr1
        stx     ptr1+1
        jsr     popax           ; dst (main)
        sta     ptr2
        stx     ptr2+1
        jmp     getstub

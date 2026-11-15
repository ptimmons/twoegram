;
; disk.s -- one raw ProDOS block read, to prove the boot volume still
; answers. On a CoGS network disk that read goes through the same card
; FIFOs as the program's HTTP requests, so it is the check that the two coexist.
;
; READ_BLOCK bypasses the file system and any ProDOS buffering: a success
; means a block really came back from the device.

        .export         _disk_read_block
        .export         _disk_unit

MLI             = $BF00
DEVNUM          = $BF30         ; unit number of the last device accessed
READ_BLOCK      = $80

        .data

rb_parms:
        .byte   3               ; parameter count
rb_unit:
        .byte   0
rb_buf: .addr   0
rb_blk: .addr   0

        .code

; unsigned char disk_unit(void) -- the unit ProDOS last used: for a program
; just launched, the volume it was loaded from. DSSS0000: drive, slot.
_disk_unit:
        lda     DEVNUM
        ldx     #0
        rts

; unsigned char __fastcall__ disk_read_block(unsigned char unit,
;                                            unsigned int block, void *buf)
; 0, or the ProDOS error code.
        .import         popa, popax
_disk_read_block:
        sta     rb_buf
        stx     rb_buf+1
        jsr     popax
        sta     rb_blk
        stx     rb_blk+1
        jsr     popa
        sta     rb_unit
        jsr     MLI
        .byte   READ_BLOCK
        .addr   rb_parms
        ldx     #0              ; A already holds the result
        rts

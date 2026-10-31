; void xms_call_far(void);
        .MODEL MEDIUM

        .DATA
        EXTRN   _xc_ax:WORD
        EXTRN   _xc_bx:WORD
        EXTRN   _xc_cx:WORD
        EXTRN   _xc_dx:WORD
        EXTRN   _xc_si:WORD
        EXTRN   _xc_di:WORD
        EXTRN   _xc_ds:WORD
        EXTRN   _xc_es:WORD
        EXTRN   _xc_flags:WORD
        EXTRN   _xc_entry:DWORD

        .CODE
        PUBLIC  _xms_call_far
_xms_call_far   PROC FAR
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    ds
        push    es

        mov     ax, _xc_ax
        mov     bx, _xc_bx
        mov     cx, _xc_cx
        mov     dx, _xc_dx
        mov     es, _xc_es
        mov     si, _xc_ds

        push    cs
        push    offset xms_return
        push    word ptr _xc_entry+2
        push    word ptr _xc_entry
        mov     ds, si
        mov     si, _xc_si
        mov     di, _xc_di
        retf

xms_return:
        push    ax
        push    bx
        push    cx
        push    dx
        pushf
        mov     bx, bp
        mov     ds, ss:[bx-6]
        pop     _xc_flags
        pop     _xc_dx
        pop     _xc_cx
        pop     _xc_bx
        pop     _xc_ax
        mov     _xc_si, si
        mov     _xc_di, di

        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        ret
_xms_call_far   ENDP

        END

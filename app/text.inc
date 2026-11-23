; ----------------------------------------------------------------------
; Page text for the clipboard.
;
; The dialog "Text der Seite" (menu Bearbeiten, object block TX_RES built by
; postbuild.pl) holds a GenText. After every screen render the text runs of
; the display list are collected in content stream order (that is the
; reading order of almost all PDFs, columns included) into one string:
; a new line where y jumps by more than half a line, a blank line where it
; jumps by more than two lines, a space where the gap to the estimated end
; of the previous run is larger than an eighth of the font size.
; The text is collected once per page (V_TXP), not again on zoom or scroll.
; In the dialog the user marks text and presses "Kopieren" (MSG_COPY to the
; GenText), or presses "Alles kopieren" (menu_copyall below).
;
; Messages of the GeoWorks 1.2 text object used here (VisText, UI.GEO; the
; method table of the class has 120 entries, the first is 0x0102):
;   0x2200  replace all text: DX:BP = string, CX = length (0 = NUL ended)
;   0x2206  select range: CX = start, DX = end (clamped to the text size)
;   0x011f  copy the selection to the clipboard
; ----------------------------------------------------------------------
%define TXM_SETTEXT 0x2200
%define TXM_SELECT  0x2206
%define TXM_COPY    0x011f

; BX:SI = the GenText of the dialog
tx_od:
 mov bx,TX_RES
 KCALL 150                        ; GeodeGetResourceHandle -> BX
 mov si,TX_CHUNK
 ret

; ObjMessage (call) to the GenText: AX = message, CX DX BP = arguments
tx_call:
 push di
 mov di,0x8000                    ; call
 call tx_msg
 pop di
 ret
; ObjMessage to the GenText with flags DI (DI destroyed)
tx_msg:
 push si
 push di
 push bx
 push ax
 push cx
 push dx
 call tx_od
 pop dx
 pop cx
 pop ax
 mov di,sp
 mov di,[ss:di+2]                 ; flags
 KCALL 169                        ; ObjMessage
 pop bx
 pop di
 pop si
 ret

; empty text (document closed)
text_clear:
 mov word [V_TXP],0
 push bp
 push si
 mov dx,cs
 mov bp,s_txempty
 xor cx,cx
 mov ax,TXM_SETTEXT
 call tx_call
 pop si
 pop bp
 ret
s_txempty: db 0

; collect the text of the current display list and put it into the GenText
; (once per page)
text_fill:
 mov ax,[V_PAGE]
 inc ax
 cmp ax,[V_TXP]                   ; same page as last time (zoom, scroll)?
 jne text_refill
 ret
text_refill:
 mov ax,[V_PAGE]
 inc ax
 mov [V_TXP],ax
 push bp
 push si
 push di
 push es
 ; text buffer: the free part of the display list block behind the DL
 ; (no extra memory block: GeoWorks reported "Speicher voll" with one)
 push ds
 mov ds,[V_DSEG]
 mov si,DL_HDR
 call dl_end                      ; SI = offset of DL_END
 pop ds
 inc si
 mov di,si
 mov cx,DL_CT-1                   ; up to the colour table
 mov ax,cx
 sub ax,di
 jc .row
 cmp ax,2048
 jae .buf
.row:
 mov di,DL_ROW                    ; DL nearly full: the 4 KB row buffers
 mov cx,DL_SIZE-1
.buf:
 mov es,[V_DSEG]
 push di
 call text_collect                ; ES:DI = NUL ended text, CX = limit
 pop bp
 mov dx,es
 xor cx,cx
 mov ax,TXM_SETTEXT
 call tx_call
 pop es
 pop di
 pop si
 pop bp
 ret

; DS:SI = display list record -> SI = offset of the DL_END byte
dl_end:
 mov al,[si]
 cmp al,1
 je .s10
 cmp al,3
 je .s10
 cmp al,2
 je .tx
 cmp al,4
 je .bm
 cmp al,5
 je .bm
 cmp al,6
 jne .d
 add si,6
 jmp short dl_end
.s10:
 add si,10
 jmp short dl_end
.tx:
 mov al,[si+11]
 xor ah,ah
 add si,ax
 add si,12
 jmp short dl_end
.bm:
 add si,[si+6]
 add si,8
 jmp short dl_end
.d:
 ret

; "Alles kopieren": text again (the user may have typed into it), mark all,
; copy to the clipboard
menu_copyall:
 cmp word [V_INIT],0
 je .r
 cmp word [V_FILE],0
 je .r
 call text_refill
 xor cx,cx                        ; mark all, then copy (queued like a
 mov dx,0x7fff                    ; click on "Kopieren")
 mov ax,TXM_SELECT
 mov di,0x4000
 call tx_msg
 mov ax,TXM_COPY
 mov di,0x4000
 call tx_msg
.r:
 retf

; ES:DI <- text of the display list in V_DSEG, at most up to ES:CX
; (DS = dgroup, preserved)
; locals: [bp-2] previous y (0x8000 = none), [bp-4] estimated end x of the
; previous run, [bp-6] its height in pixels, [bp-8] height of this run,
; [bp-10] previous x, [bp-12] previous length, [bp-14] previous width.
; Widths are estimated with the metrics of Times (serif fonts) or Helvetica
; (others), 1/100 em per character, mono = 60.
; Rotated text arrives as one run per glyph with the same x and growing y:
; such glyphs are joined, with a space where y jumps further than the
; estimated advance of the previous glyph.
text_collect:
 push ds
 push bp
 mov bp,sp
 sub sp,16
 mov [bp-16],cx                   ; end of the buffer
 mov word [bp-2],0x8000
 mov ds,[V_DSEG]
 mov si,DL_HDR
.next:
 lodsb
 cmp al,2
 je .text
 cmp al,1
 je .skip9
 cmp al,3
 je .skip9
 cmp al,4
 je .skipbm
 cmp al,5
 je .skipbm
 cmp al,6
 jne .end0                        ; DL_END or unknown
 add si,5
 jmp short .next
.end0:
 jmp .end
.skip9:
 add si,9
 jmp short .next
.skipbm:                          ; byte, x, y, len, len bytes
 add si,5
 lodsw
 add si,ax
 jmp short .next
.text:                            ; colour font size style x y len chars
 mov ax,[si+3]                    ; size in quarter points
 shr ax,1
 shr ax,1
 or ax,ax
 jnz .h
 inc ax
.h:
 mov [bp-8],ax                    ; height of this run
 mov cl,[si+10]
 xor ch,ch                        ; length
 mov ax,di                        ; room for the run, separators, "..."
 add ax,cx
 add ax,8
 cmp ax,[bp-16]
 jbe .room
 mov al,'.'
 stosb
 stosb
 stosb
 jmp .end
.room:
 mov dx,[si+8]                    ; y
 mov ax,[bp-2]
 cmp ax,0x8000
 jne .notfirst
 jmp .first
.notfirst:
 mov bx,dx
 sub bx,ax                        ; dy
 cmp byte [si+10],1               ; glyph below a glyph: rotated text
 jne .nrot
 cmp word [bp-12],1
 jne .nrot
 or bx,bx
 jle .nrot
 mov ax,[bp-6]
 shl ax,1
 cmp bx,ax
 jg .nrot
 mov ax,[si+6]
 sub ax,[bp-10]
 jns .rabs
 neg ax
.rabs:
 mov cx,[bp-6]
 shr cx,1
 shr cx,1
 cmp ax,cx
 ja .nrot
 mov ax,bx                        ; gap = dy - advance of the previous glyph
 sub ax,[bp-14]
 jmp short .gap
.nrot:
 mov ax,bx
 or ax,ax
 jns .abs
 neg ax
.abs:
 mov cx,[bp-6]
 shr cx,1                         ; half a line
 cmp ax,cx
 jbe .sameline
 mov al,0x0d                      ; new line
 stosb
 mov cx,[bp-6]
 shl cx,1
 cmp bx,cx                        ; more than two lines down: blank line
 jle .first
 stosb
 jmp short .first
.sameline:
 mov ax,[si+6]                    ; gap to the end of the previous run
 sub ax,[bp-4]
.gap:
 mov cx,[bp-8]
 shr cx,1
 shr cx,1
 shr cx,1                         ; an eighth of the font size
 cmp ax,cx
 jle .first
 cmp byte [es:di-1],' '
 je .first
 cmp byte [si+11],' '
 je .first
 mov al,' '
 stosb
.first:
 mov [bp-2],dx
 mov ax,[bp-8]
 mov [bp-6],ax
 mov cl,[si+10]
 xor ch,ch
 mov [bp-12],cx
 mov ax,[si+6]
 mov [bp-10],ax
 call tx_width                    ; AX = estimated width in pixels
 mov [bp-14],ax
 add ax,[si+6]
 mov [bp-4],ax
 mov cl,[si+10]
 xor ch,ch
 add si,11
 jcxz .next2
.copy:
 lodsb
 cmp al,0x20
 jae .c
 mov al,' '
.c:
 stosb
 loop .copy
.next2:
 jmp .next
.end:
 mov byte [es:di],0
 mov sp,bp
 pop bp
 pop ds
 ret

; DS:SI = text record (after the opcode), [bp-8] = height in pixels
; -> AX = estimated width of the run in pixels (BX CX DX destroyed)
tx_width:
 push si
 mov cl,[si+10]
 xor ch,ch
 mov bx,tx_helv
 mov ax,[si+1]                    ; font
 cmp ax,0x1a00                    ; mono
 jne .nm
 mov ax,60
 mul cx
 jmp short .scale
.nm:
 cmp ax,0x1000                    ; serif
 jne .ns
 mov bx,tx_times
.ns:
 add si,11
 xor dx,dx                        ; sum in 1/100 em
 jcxz .scale0
.l:
 lodsb
 mov ah,50                        ; GEOS characters >= 0x80
 cmp al,0x80
 jae .add
 mov ah,25
 sub al,0x20
 jb .add
 cmp al,94
 ja .add
 push bx
 add bl,al
 adc bh,0
 mov ah,[cs:bx]
 pop bx
.add:
 mov al,ah
 xor ah,ah
 add dx,ax
 loop .l
.scale0:
 mov ax,dx
.scale:
 mul word [bp-8]
 mov cx,100
 div cx
 pop si
 ret

tx_times:
 db 25,33,41,50,50,83,78,33,33,33,50,56,25,33,25,28
 db 50,50,50,50,50,50,50,50,50,50,28,28,56,56,56,44
 db 92,72,67,67,72,61,56,72,72,33,39,72,61,89,72,72
 db 56,72,67,56,61,72,72,94,72,72,61,33,28,33,47,50
 db 33,44,50,44,50,44,33,50,50,28,28,50,28,78,50,50
 db 50,50,33,39,28,50,50,72,50,50,44,48,20,48,54
tx_helv:
 db 28,28,36,56,56,89,67,22,33,33,39,58,28,33,28,28
 db 56,56,56,56,56,56,56,56,56,56,28,28,58,58,58,56
 db 102,67,67,72,72,67,61,78,72,28,50,67,56,83,72,78
 db 67,78,72,67,61,72,67,94,67,67,61,28,28,28,47,56
 db 22,56,56,50,56,56,28,56,56,22,22,50,22,83,56,56
 db 56,56,33,50,28,56,50,72,50,50,50,33,26,33,58

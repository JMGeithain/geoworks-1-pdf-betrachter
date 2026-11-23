; SPDX-License-Identifier: MIT
; PDF-Betrachter: help page in the style of the GeoWorks help windows (as in
; GeoMaler / Solitaire Help): dark title bar, white text page with bold
; headings and bullets, scroll bar, buttons "Seite hoch", "Seite runter",
; "Schliessen". Lines: help_text (helptext.inc, made by tools/gen_help.py),
; kind byte H/S/T/B/E + text + 0.

%define H_SB      SB
%define H_TTOP    (VIEW_TOP+30)
%define H_BTNW    116
%define H_BTNH    22

; text at AX,BX from CS:SI
text_cs:
 push ds
 push cs
 pop ds
 call text_ds
 pop ds
 ret

; raised (bevel4) 3D box AX,BX - CX,DX; keeps AX..DX
bevel4:
 mov byte [B_SUNK],0
bevel4s:
 mov [B_L],ax
 mov [B_T],bx
 mov [B_R],cx
 mov [B_B],dx
 push ax
 push bx
 push cx
 push dx
 call bevel
 pop dx
 pop cx
 pop bx
 pop ax
 ret

; black 1 pixel frame around AX,BX - CX,DX (exclusive); keeps AX..DX
frame4:
 push ax
 push bx
 push cx
 push dx
 push ax
 xor al,al
 call set_area
 pop ax
 push dx                          ; top
 mov dx,bx
 inc dx
 call fill4
 pop dx
 push bx                          ; bottom
 mov bx,dx
 dec bx
 call fill4
 pop bx
 push cx                          ; left
 mov cx,ax
 inc cx
 call fill4
 pop cx
 mov ax,cx                        ; right
 dec ax
 call fill4
 pop dx
 pop cx
 pop bx
 pop ax
 ret
fill4:
 push ax
 push bx
 push cx
 push dx
 call fill
 pop dx
 pop cx
 pop bx
 pop ax
 ret

; height of a line of kind AL -> AX
help_lh:
 mov ah,26
 cmp al,'H'
 je .r
 mov ah,8
 cmp al,'E'
 je .r
 mov ah,15
.r:
 mov al,ah
 xor ah,ah
 ret

; CS:SI at a line -> SI at the next one
help_next:
 inc si
.s:
 cmp byte [cs:si],0
 je .e
 inc si
 jmp .s
.e:
 inc si
 ret

; font URW Sans: DX = size, AL = style bits (0x20 bold)
help_font:
 push ax
 push di
 mov cx,0x1200
 xor ah,ah
 KCALL 366                        ; GrSetFont
 pop di
 pop ax
 push di
 mov ah,al
 not ah
 KCALL 362                        ; GrSetTextStyle
 pop di
 ret

; help page (mode 3); DI = gstate
paint_help:
 mov es,[V_LSEG]
 mov ax,[V_VH]
 sub ax,H_BTNH+8
 mov [es:LH_BTN],ax
 sub ax,8
 mov [es:LH_BOT],ax
 mov ax,[V_VW]
 sub ax,8+H_SB
 mov [es:LH_SBX],ax
 ; dialog face
 mov al,7
 call set_area
 mov ax,4
 mov bx,VIEW_TOP+2
 mov cx,[V_VW]
 sub cx,4
 mov dx,[V_VH]
 sub dx,2
 call fill
 ; title bar
 mov al,8
 call set_area
 mov ax,8
 mov bx,VIEW_TOP+6
 mov cx,[V_VW]
 sub cx,8
 mov dx,VIEW_TOP+24
 call fill
 mov al,15
 call set_tcol
 mov dx,12
 mov al,0x20
 call help_font
 mov si,s_htitle
 mov ax,16
 mov bx,VIEW_TOP+8
 call text_cs
 ; white page with a black frame
 mov al,15
 call set_area
 mov es,[V_LSEG]
 mov ax,8
 mov bx,H_TTOP
 mov cx,[es:LH_SBX]
 mov dx,[es:LH_BOT]
 call fill
 mov es,[V_LSEG]
 mov ax,7
 mov bx,H_TTOP-1
 mov cx,[es:LH_SBX]
 add cx,H_SB+1
 mov dx,[es:LH_BOT]
 inc dx
 call frame4
 ; count the lines
 mov si,help_text
 xor cx,cx
.cnt:
 cmp byte [cs:si],0
 je .cntd
 call help_next
 inc cx
 jmp .cnt
.cntd:
 mov es,[V_LSEG]
 mov [es:LH_N],cx
 mov ax,[es:LH_TOP]
 cmp ax,cx
 jb .t0
 mov word [es:LH_TOP],0
.t0:
 mov si,help_text                 ; skip to the first shown line
 mov cx,[es:LH_TOP]
 jcxz .sk
.skl:
 call help_next
 loop .skl
.sk:
 xor al,al
 call set_tcol
 mov bx,H_TTOP+6                  ; y
 mov es,[V_LSEG]
 mov word [es:LH_ROWS],0
.line:
 mov al,[cs:si]
 or al,al
 jnz .l1
 jmp .ldone
.l1:
 push ax
 call help_lh
 mov dx,bx
 add dx,ax
 mov es,[V_LSEG]
 cmp dx,[es:LH_BOT]
 pop ax
 jbe .l2
 jmp .ldone
.l2:
 push dx                          ; next y
 push si
 cmp al,'H'
 jne .nh
 push bx
 mov dx,12
 mov al,0x20
 call help_font
 pop bx
 inc si
 mov ax,18
 add bx,8
 call text_cs
 jmp .ln
.nh:
 cmp al,'E'
 je .ln
 mov cl,al
 push cx
 push bx
 mov dx,10
 xor al,al
 cmp cl,'S'
 jne .ns
 mov al,0x20
.ns:
 call help_font
 pop bx
 pop cx
 inc si
 mov ax,18
 cmp cl,'B'
 jne .txt
 push si
 push bx
 mov si,s_bullet
 mov ax,22
 call text_cs
 pop bx
 pop si
 mov ax,34
.txt:
 call text_cs
.ln:
 pop si
 call help_next
 pop bx
 mov es,[V_LSEG]
 inc word [es:LH_ROWS]
 jmp .line
.ldone:
 call help_scrollbar
 ; buttons
 mov dx,10
 xor al,al
 call help_font
 mov ax,20
 mov si,s_hb0
 call help_button
 mov ax,[V_VW]
 sub ax,H_BTNW
 shr ax,1
 mov si,s_hb1
 call help_button
 mov ax,[V_VW]
 sub ax,H_BTNW+20
 mov si,s_hb2
 call help_button
 xor al,al                        ; plain style for the status bar
 mov dx,10
 call help_font
 ret

; button with label CS:SI at x = AX, y = LH_BTN
help_button:
 push si
 mov es,[V_LSEG]
 mov bx,[es:LH_BTN]
 mov cx,ax
 add cx,H_BTNW
 mov dx,bx
 add dx,H_BTNH
 call bevel4
 call frame4
 pop si                           ; label roughly centred (6 pixels a character)
 push ax
 push bx
 xor cx,cx
.len:
 cmp byte [cs:si],0
 je .lend
 inc si
 inc cx
 jmp .len
.lend:
 sub si,cx
 mov ax,cx
 mov cl,6
 mul cl
 mov cx,H_BTNW
 sub cx,ax
 sar cx,1
 pop bx
 pop ax
 add ax,cx
 add bx,5
 push ax
 push bx
 xor al,al
 call set_tcol
 pop bx
 pop ax
 jmp text_cs

; scroll bar: arrow buttons, sunken track, thumb
help_scrollbar:
 mov es,[V_LSEG]
 mov ax,[es:LH_SBX]
 mov bx,H_TTOP
 mov cx,ax
 add cx,H_SB
 mov dx,[es:LH_BOT]
 mov byte [B_SUNK],1
 call bevel4s
 push ax                          ; dark track inside
 mov al,8
 call set_area
 pop ax
 push ax
 push cx
 inc ax
 dec cx
 call fill4
 pop cx
 pop ax
 mov dx,bx                        ; up arrow
 add dx,H_SB
 mov [B_L],ax
 mov [B_T],bx
 mov [B_R],cx
 mov [B_B],dx
 mov al,0
 call arrow_btn
 mov es,[V_LSEG]                  ; down arrow
 mov ax,[es:LH_SBX]
 mov dx,[es:LH_BOT]
 mov [B_L],ax
 add ax,H_SB
 mov [B_R],ax
 mov [B_B],dx
 sub dx,H_SB
 mov [B_T],dx
 mov al,1
 call arrow_btn
 ; thumb: start and length proportional to the lines
 mov es,[V_LSEG]
 mov cx,[es:LH_BOT]
 sub cx,H_TTOP+2*H_SB             ; track length
 jle .r
 mov bx,[es:LH_N]
 or bx,bx
 jz .r
 mov ax,[es:LH_TOP]
 mul cx
 div bx
 add ax,H_TTOP+H_SB
 mov [es:LH_T0],ax
 mov ax,[es:LH_ROWS]
 mul cx
 div bx
 cmp ax,12
 jge .tl
 mov ax,12
.tl:
 add ax,[es:LH_T0]
 mov dx,[es:LH_BOT]
 sub dx,H_SB
 cmp ax,dx
 jle .tm
 mov ax,dx
.tm:
 mov [es:LH_T1],ax
 mov dx,ax
 mov bx,[es:LH_T0]
 mov ax,[es:LH_SBX]
 mov cx,ax
 add cx,H_SB
 call bevel4
.r:
 ret

; scroll by AX lines (signed)
help_scroll:
 mov es,[V_LSEG]
 add ax,[es:LH_TOP]
 jge .a
 xor ax,ax
.a:
 push ax
 call help_maxtop
 pop ax
 cmp ax,bx
 jle .c
 mov ax,bx
.c:
 mov es,[V_LSEG]
 mov [es:LH_TOP],ax
 ret
; first line from which the rest fits on the page -> BX
help_maxtop:
 mov es,[V_LSEG]
 mov dx,[es:LH_BOT]
 sub dx,H_TTOP+6
 xor bx,bx
 mov si,help_text
.t:
 push si
 xor cx,cx
.s:
 mov al,[cs:si]
 or al,al
 jz .se
 call help_lh
 add cx,ax
 call help_next
 jmp .s
.se:
 pop si
 cmp cx,dx
 jbe .r
 call help_next
 inc bx
 jmp .t
.r:
 ret
help_page:                        ; AX = +1 / -1
 mov es,[V_LSEG]
 mov bx,[es:LH_ROWS]
 dec bx
 jg .p
 mov bx,1
.p:
 imul bx
 jmp help_scroll

; click in the help page: CX,DX window coordinates.
; Sets V_MODE = 0 for "Schliessen"; the caller redraws.
help_click:
 mov es,[V_LSEG]
 mov ax,dx
 sub ax,[es:LH_BTN]
 jl .sb
 cmp ax,H_BTNH
 jge .r
 cmp cx,20                        ; buttons
 jl .r
 cmp cx,20+H_BTNW
 jl .up
 mov ax,[V_VW]
 sub ax,H_BTNW
 shr ax,1
 cmp cx,ax
 jl .r
 add ax,H_BTNW
 cmp cx,ax
 jl .down
 mov ax,[V_VW]
 sub ax,H_BTNW+20
 cmp cx,ax
 jl .r
 add ax,H_BTNW
 cmp cx,ax
 jge .r
 mov word [V_MODE],0              ; Schliessen
 ret
.sb:
 mov ax,[es:LH_SBX]
 cmp cx,ax
 jl .r
 add ax,H_SB
 cmp cx,ax
 jge .r
 cmp dx,H_TTOP
 jl .r
 cmp dx,H_TTOP+H_SB
 jl .line_up
 mov ax,[es:LH_BOT]
 cmp dx,ax
 jge .r
 sub ax,H_SB
 cmp dx,ax
 jge .line_down
 cmp dx,[es:LH_T0]
 jl .up
 cmp dx,[es:LH_T1]
 jg .down
 ret
.line_up:
 mov ax,-3
 jmp help_scroll
.line_down:
 mov ax,3
 jmp help_scroll
.up:
 mov ax,-1
 jmp help_page
.down:
 mov ax,1
 jmp help_page
.r:
 ret

; key in the help page: CL, CH. Esc / Enter: V_MODE = 0
help_key:
 cmp cl,0x1b
 je .close
 cmp cl,0x0d
 je .close
 cmp ch,0xff
 jne .asc
 cmp cl,0x90
 je .up1
 cmp cl,0x91
 je .dn1
 cmp cl,0x96
 je .pup
 cmp cl,0x97
 je .pdn
.asc:
 cmp cl,'8'
 je .up1
 cmp cl,'k'
 je .up1
 cmp cl,'2'
 je .dn1
 cmp cl,'j'
 je .dn1
 cmp cl,'9'
 je .pup
 cmp cl,'3'
 je .pdn
 cmp cl,' '
 je .pdn
 ret
.close:
 mov word [V_MODE],0
 ret
.up1:
 mov ax,-1
 jmp help_scroll
.dn1:
 mov ax,1
 jmp help_scroll
.pup:
 mov ax,-1
 jmp help_page
.pdn:
 mov ax,1
 jmp help_page

s_htitle: db 'Hilfe zum PDF-Betrachter',0
s_bullet: db 0xa5,0
s_hb0:    db 'Seite hoch',0
s_hb1:    db 'Seite runter',0
s_hb2:    db 'Schlie',0xa7,'en',0

%include "helptext.inc"

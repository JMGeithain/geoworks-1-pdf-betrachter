; SPDX-License-Identifier: MIT
; PDF-Betrachter for GeoWorks Ensemble 1.2 (8086, NASM, SDK ensemble12 adapter).
; The PDF work happens in the import-free library PDFCORE.GEO (import 1):
;   ordinal 0 pdf_open(file, fsize:dword, readfn:far, work:far, worksize)
;   ordinal 1 pdf_page_count(work:far)
;   ordinal 2 pdf_page_size(work:far, page, w:far, h:far)   (points)
;   ordinal 3 pdf_render(work:far, page, scale256, dl:far, dlsize, trunc:far)
;   ordinal 4 pdf_errtext(err, buf:far, size)
; All are FAR __cdecl: arguments pushed right to left, caller pops,
; AX/BX/CX/DX/ES volatile, SI/DI/BP/DS preserved, result in AX.
;
; Version 1 opens DOCUMENT\TEST.PDF (directory listing follows once the
; FileEnum ordinal of the German 1.2.8 kernel is confirmed).
%include "ensemble12.inc"
%define PROCESS_CLASS 0x40

; ---- process DGROUP variables ----
; The method table starts at 0x52 and takes 6 bytes per G1_METHOD; it must
; end below 0xA0, so at most 13 methods. (11 used.)
%define V_WSEG    0xA0      ; work block segment (locked for the app's life)
%define V_WHAN    0xA2      ; work block handle
%define V_DSEG    0xA4      ; display list block segment
%define V_DHAN    0xA6      ; display list block handle
%define V_FILE    0xA8      ; file handle, 0 = none
%define V_PAGE    0xAA      ; current page (0-based)
%define V_NPAGES  0xAC
%define V_SX      0xAE      ; scroll offset in page pixels
%define V_SY      0xB0
%define V_WIN     0xB2      ; last exposed window handle, 0 = closed
%define V_ERR     0xB4      ; last error (0 = ok)
%define V_PW      0xB6      ; page size in device pixels
%define V_PH      0xB8
%define V_ZOOM    0xBA      ; pixels per point * 256
%define V_INIT    0xBC      ; 1 after first initialisation
%define V_VW      0xBE      ; window width/height in pixels
%define V_VH      0xC0
%define V_FSLO    0xC2
%define V_FSHI    0xC4
%define V_TRUNC   0xC6      ; byte set by pdf_render
%define V_PTW     0xC8      ; page size in points
%define V_PTH     0xCA
%define V_TMP     0xCC
%define V_LSEG    0xD0      ; file list block segment / handle
%define V_LHAN    0xD2
%define V_MODE    0xD4      ; 0 = page, 1 = file list
%define V_DISK    0xD6      ; disk handle of the current directory
%define V_LSEL    0xD8      ; selected list row
%define V_LTOP    0xDA      ; first visible row
%define V_NPDF    0xDC      ; number of PDF files in the list
%define V_NENT    0xDE      ; entries returned by FileEnum
; file list block layout
%define L_CUR     0         ; current path from FileGetCurrentPath
%define L_DIR     128       ; DOCUMENT path
%define L_FULL    256       ; full path of the file to open
%define L_DIAG    384       ; diagnostic text
%define L_MSG     512       ; 64 bytes status / error text
%define MSG_SIZE  64
%define L_ENT     640       ; FileEnum entries, ENT_SIZE bytes each
%define ENT_SIZE  22        ; attr(1) date/time(4) size(4) DOS name(13)
%define ENT_NAME  9
%define MAX_ENT   120
%define L_NAMES   (L_ENT+ENT_SIZE*MAX_ENT+16)  ; NAME_SZ bytes per list row
%define NAME_SZ   16        ; shown name ("[DIR]" for folders), NUL, type at +15
%define NAME_TY   15        ; 0 = PDF, 1 = folder, 2 = parent folder
%define MAX_PDF   100       ; list rows (folders and PDFs)
%define L_XSEG    (L_NAMES+NAME_SZ*MAX_PDF+16)   ; xref table block segment
%define L_XHAN    (L_XSEG+2)                ; and handle
%define L_PASS    (L_XSEG+4)                ; byte: 1 = collect folders, 2 = PDFs
%define L_DIRSET  (L_XSEG+5)                ; byte: L_DIR holds the list folder
%define L_SKIP    (L_XSEG+6)                ; word: FileEnum skip count
%define L_SHOWD   (L_XSEG+8)                ; byte: show L_DIAG instead of the hint
%define L_PCH     (L_XSEG+10)               ; printing: PrintControl handle,
%define L_PCC     (L_XSEG+12)               ; chunk,
%define L_PGS     (L_XSEG+14)               ; gstate (print GString)
%define L_ML      (L_XSEG+16)               ; left / top margin
%define L_MT      (L_XSEG+18)
%define L_PRW     (L_XSEG+20)               ; printable width / height
%define L_PRH     (L_XSEG+22)
%define L_SAVE    (L_XSEG+24)               ; 5 words: zoom, sx, sy, vh, mode
%define L_PPW     (L_XSEG+34)               ; paper width / height (diagnosis)
%define L_PPH     (L_XSEG+36)
%define L_PRDIAG  (L_XSEG+38)               ; byte: status bar shows L_DIAG
%define L_DOCN    (L_XSEG+40)               ; 34 bytes: document name for printing
%define L_OX      (L_XSEG+74)               ; view origin (the view can scroll
%define L_OY      (L_XSEG+76)               ; by itself, e.g. mouse wheel)
%define L_POX     (L_XSEG+78)               ; origin at the last full paint
%define L_POY     (L_XSEG+80)
%define L_NAMEOK  (L_XSEG+82)               ; byte: document name was asked for
%define L_PK      (L_XSEG+86)               ; printing: page counter
%define L_PN      (L_XSEG+88)               ; pages to print
%define L_EXTP    (L_XSEG+90)               ; NEXT far pointers for pdf_set_ext
%define L_SIZE    (L_XSEG+90+4*NEXT)
%define PC_CHUNK  0x3c                      ; PrintControl chunk in resource 3 (postbuild.pl)
%define MAX_PRINT 70                        ; pages per print job (70 * A4 < 64K points)
%define XREF_SIZE 40000                     ; 10000 object entries (PDF 1.5 xref streams)
%define ROW_H     16
%define LIST_Y    (VIEW_TOP+52)
%define TRIES     6
%define V_TRY     0xE0      ; 12 bytes: per attempt path/open result
%define V_FW      0xEC      ; full window width (V_VW/V_VH = page area)
%define V_FH      0xEE
%define V_ALB     0xF0      ; segment of the copied launch data, 0 = none
; copy of AppLaunchBlock bytes 0x9F..0x17E (as built by GeoManager 1.2)
%define ALB_FROM  0x9f
%define ALB_LEN   0xe0
%define S_DISK    0         ; ALB 0x9F: disk handle of the document
%define S_PATH    2         ; ALB 0xA1: folder of the document
%define S_NAME    0x6a      ; ALB 0x109: document name
%define S_HAN     0xfe      ; handle of the copy
%define SB        15        ; scroll bar thickness
; scratch for 3D drawing (the open-attempt codes at 0xF0 are no longer needed)
%define B_L       0xE0
%define B_T       0xE2
%define B_R       0xE4
%define B_B       0xE6
%define B_SUNK    0xE8
%define B_DIR     0xE9
%define B_CX      0xEA
%define B_CY      0xCE
%define BTN_W     22        ; status bar button width

%define WORK_SIZE 64000
%define DL_SIZE   64000
%define DL_TMPSZ  4096      ; end of the DL block: row buffers for enlarged bitmaps
%define DL_CTSZ   960       ; before them: true colour table (240 x r,g,b,index)
%define DL_CT     (DL_SIZE-DL_TMPSZ-DL_CTSZ)
%define DL_ROW    (DL_SIZE-DL_TMPSZ)
%define DL_TMP    (DL_SIZE-DL_TMPSZ+1100)
%define DL_HDR    48        ; display list block: header, then records
%define NEXT      1         ; external image blocks (DL_SIZE each, per page)
%define BAR_H     18        ; status bar height in pixels
%define VIEW_TOP  BAR_H

; DL block header fields (block offsets)
%define H_SX      0
%define H_SY      2
%define H_COL     4         ; last area colour, 0xFF = unknown
%define H_TCOL    5
%define H_FONT    6
%define H_SIZE    8
%define H_STYLE   10
%define H_VH      12
; replay scratch (also in the DL block header, DS = DL block)
%define V_RX0     14
%define V_RY0     16
%define V_RFONT   18
%define V_RSIZE   20
%define V_RSTYLE  22
%define V_RLEN    24
%define H_TC      27        ; true colour display (64K driver), key e
%define H_PRN     28        ; printing: render without true colour
%define H_EXTH    32        ; handles of the external image blocks (NEXT words)
%define H_EXTS    40        ; their segments (locked while the page is shown)
%define H_TCD     29        ; GEOS.INI check: 1 path 2 open 3 section 4 device 5 no key 9 ok

G1_METHOD 0x0a01, app_open
G1_METHOD 0x0a03, app_close
G1_METHOD 0x0004, meta_attach
G1_METHOD 0x0057, view_closed
G1_METHOD 0x0049, view_exposed
G1_METHOD 0x007b, kbd_char
G1_METHOD 0x00c3, mouse_select
G1_METHOD 0x0c00, menu_open
G1_METHOD 0x0c01, menu_close
G1_METHOD 0x0c02, menu_print
G1_METHOD 0x0c03, menu_info
G1_METHOD 0x0c04, menu_help
G1_METHOD 0x0259, print_docname

; ======================================================================
; lifecycle (as in the SDK starter)
; ======================================================================
app_open:
 push es
 pop ds
 call grab_alb                    ; document passed by GeoManager?
 mov di,PROCESS_CLASS
 KCALL 489                        ; ObjCallSuperNoLock
 retf

app_close:
 push es
 pop ds
 call shutdown
 xor cx,cx
 retf

meta_attach:
 mov di,PROCESS_CLASS
 KCALL 489
 retf

; DX = AppLaunchBlock handle (DS = dgroup): keep the document part
grab_alb:
 push ax
 push bx
 push cx
 push dx
 push si
 push di
 push es
 push ds
 or dx,dx
 jz .r
 test dl,0x0f                     ; handles are paragraph aligned
 jnz .r
 cmp word [V_ALB],0
 jne .r
 mov bx,dx
 KCALL 5                          ; MemLock -> AX
 jc .r
 push bx
 push ax
 mov ax,0x100
 mov cx,0xc000
 KCALL 0                          ; MemAlloc (zeroed, locked)
 pop si                           ; launch block segment
 jc .un
 mov [V_ALB],ax
 mov es,ax
 mov [es:S_HAN],bx
 mov ds,si
 mov si,ALB_FROM
 xor di,di
 mov cx,ALB_LEN
 rep movsb
.un:
 pop bx
 KCALL 6                          ; MemUnlock the launch block
.r:
 pop ds
 pop es
 pop di
 pop si
 pop dx
 pop cx
 pop bx
 pop ax
 ret

view_closed:
 mov word [V_WIN],0
 mov di,PROCESS_CLASS
 KCALL 489
 retf

; ======================================================================
; drawing
; ======================================================================
view_exposed:
 mov [V_WIN],cx
 push cx
 cmp word [V_INIT],0
 jne .ready
 mov di,cx
 KCALL 211                        ; GrCreateState (only for window bounds)
 call get_bounds
 KCALL 212                        ; GrDestroyState
 call startup
.ready:
 pop di
 KCALL 211                        ; GrCreateState
 KCALL 446                        ; GrBeginUpdate
 call get_bounds
 call paint
 KCALL 447                        ; GrEndUpdate
 KCALL 212                        ; GrDestroyState
 ; the view scrolled by itself: the rest of the window still shows the
 ; old picture moved, so paint everything once more
 cmp word [V_LSEG],0
 je .r
 mov es,[V_LSEG]
 mov ax,[es:L_OX]
 mov bx,[es:L_OY]
 cmp ax,[es:L_POX]
 jne .moved
 cmp bx,[es:L_POY]
 je .r
.moved:
 mov [es:L_POX],ax
 mov [es:L_POY],bx
 call repaint
.r:
 retf

; redraw outside an exposed event (after a key press)
repaint:
 mov di,[V_WIN]
 or di,di
 jz .no
 KCALL 211
 call get_bounds
 call paint
 KCALL 212
.no:
 ret

; DI = gstate. Stores window size in V_VW/V_VH.
get_bounds:
 push di
 KCALL 440                        ; GrGetWinBounds -> AX,BX,CX,DX
 pop di
 ; the GenView may have scrolled (mouse wheel): draw relative to the
 ; visible top left corner
 cmp word [V_LSEG],0
 je .nos
 push es
 mov es,[V_LSEG]
 mov [es:L_OX],ax
 mov [es:L_OY],bx
 pop es
.nos:
 push ax
 push bx
 push cx
 push dx
 mov si,ax
 or si,bx
 jz .not
 mov dx,ax
 xor cx,cx
 xor ax,ax
 push di
 KCALL 310                        ; GrApplyTranslation (left, top)
 pop di
.not:
 pop dx
 pop cx
 pop bx
 pop ax
 sub cx,ax                        ; bounds are inclusive: +1
 sub dx,bx
 inc cx
 inc dx
 mov [V_FW],cx
 mov [V_FH],dx
 sub cx,SB                        ; page area leaves room for the bars
 sub dx,SB
 mov [V_VW],cx
 mov [V_VH],dx
 ret

; DI = gstate, DS = dgroup
paint:
 ; grey desk
 mov ax,8
 call set_area
 xor ax,ax
 mov bx,VIEW_TOP
 mov cx,[V_VW]
 mov dx,[V_VH]
 call fill
 cmp word [V_MODE],0
 je .pagemode
 cmp word [V_MODE],1
 jne .textpage
 call paint_list
 jmp .bar
.textpage:
 call paint_text
 jmp .bar
.pagemode:
 call clamp_scroll
 cmp word [V_ERR],0
 jne .error
 ; white page
 mov ax,15
 call set_area
 xor ax,ax
 sub ax,[V_SX]
 mov bx,VIEW_TOP
 sub bx,[V_SY]
 mov cx,ax
 add cx,[V_PW]
 mov dx,bx
 add dx,[V_PH]
 call fill
 call replay
 jmp .bar
.error:
 mov ax,12
 call set_area
 mov ax,12
 mov bx,VIEW_TOP+12
 mov cx,[V_VW]
 sub cx,12
 mov dx,VIEW_TOP+40
 call fill
 xor ax,ax
 call set_tcol
 push ds
 mov ds,[V_LSEG]
 mov si,L_MSG
 mov ax,20
 mov bx,VIEW_TOP+18
 call text_ds
 pop ds
.bar:
 call scroll_bars
 call status_bar
 ret

; AL = palette index
set_area:
 push di
 xor ah,ah
 KCALL 355                        ; GrSetAreaColor
 pop di
 ret
set_tcol:
 push di
 xor ah,ah
 KCALL 359                        ; GrSetTextColor
 pop di
 ret
; AX,BX,CX,DX = left,top,right,bottom (exclusive)
fill:
 push di
 KCALL 336                        ; GrFillRect
 pop di
 ret
; DS:SI NUL terminated, AX,BX = position
text_ds:
 push di
 push si
 xor cx,cx
 KCALL 330                        ; GrDrawText
 pop si
 pop di
 ret

status_bar:
 mov ax,7
 call set_area
 xor ax,ax
 xor bx,bx
 mov cx,[V_FW]                    ; full width: also the corner above the bar
 mov dx,BAR_H
 call fill
 push di
 mov cx,0x1200                    ; URW Sans
 mov dx,10
 xor ah,ah
 KCALL 366                        ; GrSetFont
 pop di
 push di
 mov ax,0xff00                    ; clear all styles
 KCALL 362                        ; GrSetTextStyle
 pop di
 xor ax,ax
 call set_tcol
 ; build "Seite n / m   Zoom z%   ..."
 cmp word [V_LSEG],0
 je .nomsg
 push di
 mov es,[V_LSEG]
 mov di,L_MSG
 cmp word [V_MODE],0
 je .pg
 mov si,s_lstat
 cmp word [V_MODE],1
 je .ls
 mov si,s_istat
.ls:
 call copy_cs
 jmp .t
.pg:
 cmp byte [es:L_PRDIAG],0
 je .pg1
 push ds
 mov ds,[V_LSEG]
 mov si,L_DIAG
.pgd:
 lodsb
 stosb
 or al,al
 jnz .pgd
 pop ds
 dec di
 jmp .t
.pg1:
 mov si,s_page
 call copy_cs
 mov ax,[V_PAGE]
 inc ax
 call put_num
 mov si,s_of
 call copy_cs
 mov ax,[V_NPAGES]
 call put_num
 mov si,s_zoom
 call copy_cs
 mov ax,[V_ZOOM]
 mov cx,100
 mul cx
 mov al,ah
 mov ah,dl                        ; (zoom*100)>>8
 call put_num
 mov al,'%'
 stosb
 push ds                          ; colour mode and GEOS.INI check
 mov ds,[V_DSEG]
 mov al,[H_TC]
 mov ah,[H_TCD]
 pop ds
 mov si,s_tc1
 or al,al
 jnz .tcs
 push ax
 mov si,s_tc0
 call copy_cs
 pop ax
 mov al,ah
 add al,'0'
 stosb
 mov al,')'
 stosb
 jmp .tce
.tcs:
 call copy_cs
.tce:
 cmp byte [V_TRUNC],0
 je .t
 mov si,s_trunc
 call copy_cs
.t:
 mov byte [es:di],0
 pop di
 cmp word [V_MODE],0
 jne .show
 cmp word [V_ERR],0
 jne .skip                        ; error text already shown in the view
.show:
 push ds
 mov ds,[V_LSEG]
 mov si,L_MSG
 mov ax,6
 mov bx,2
 call text_ds
 pop ds
.skip:
.nomsg:
 call buttons
 ret

; copy NUL string CS:SI -> ES:DI (no terminator)
copy_cs:
 mov al,[cs:si]
 or al,al
 jz .d
 mov [es:di],al
 inc si
 inc di
 jmp copy_cs
.d:
 ret

; AX unsigned -> decimal at ES:DI
put_num:
 mov cx,10
 xor bx,bx
.div:
 xor dx,dx
 div cx
 push dx
 inc bx
 or ax,ax
 jnz .div
.out:
 pop ax
 add al,'0'
 mov [es:di],al
 inc di
 dec bx
 jnz .out
 ret

s_page:  db 'Seite ',0
s_lstat: db 'Dateiauswahl: Klick oder Eingabe',0
s_istat: db 'Klick oder Taste: zur',0x9f,'ck',0
s_of:    db '/',0
s_zoom:  db '  Zoom ',0
s_tc1:   db '  Echtfarben',0
s_tc0:   db '  16 Farben (',0
s_help:  db 'Ziffernblock: 9/3 Seite  8/2/4/6 schieben  +/- Zoom',0
s_trunc: db '  (gekuerzt)',0

; ----------------------------------------------------------------------
; replay the display list. DI = gstate, DS = dgroup on entry and exit.
; ----------------------------------------------------------------------
replay:
 mov ax,[V_SX]
 mov bx,[V_SY]
 mov cx,[V_VH]
 push ds
 mov ds,[V_DSEG]
 mov [H_SX],ax
 sub bx,VIEW_TOP                  ; page y -> window y = y - sy + VIEW_TOP
 mov [H_SY],bx
 mov [H_VH],cx
 mov byte [H_COL],0xff
 mov byte [H_TCOL],0xff
 mov word [H_FONT],0
 mov word [H_SIZE],0
 mov word [H_STYLE],0xffff
 mov si,DL_HDR
.next:
 lodsb
 cmp al,1
 je .rect
 cmp al,2
 je .text
 cmp al,4
 je .bitmap
 cmp al,5
 je .rgbjmp
 cmp al,6
 jne .done                        ; DL_END or unknown
 jmp .ext
.rgbjmp:
 jmp .rgbimg
.bitmap:                          ; colour, x, y, len, GEOS Bitmap
 lodsb
 mov dl,al
 lodsw
 sub ax,[H_SX]
 mov [V_RX0],ax
 lodsw
 sub ax,[H_SY]
 mov bx,ax
 lodsw
 mov cx,ax                        ; bytes of the Bitmap
 mov ax,si
 add ax,cx
 push ax                          ; next record
 test byte [si+5],0x0f            ; colour bitmap: drawn row by row
 jz .bm1
 jmp .bm2
.bm1:
 cmp bx,[H_VH]                    ; below the window?
 jge .bmskip
 mov ax,bx
 add ax,[si+2]                    ; + height
 cmp ax,VIEW_TOP                  ; above the page area?
 jle .bmskip
 mov ax,[V_RX0]
 add ax,[si]
 jle .bmskip                      ; left of the window
 cmp dl,[H_COL]
 je .bmcol
 mov [H_COL],dl
 push bx
 push si
 call col_regs
 push di
 KCALL 355                        ; GrSetAreaColor (mono bitmaps)
 pop di
 pop si
 pop bx
.bmcol:
 mov ax,[V_RX0]
 xor dx,dx                        ; no callback
 xor cx,cx
 push di
 KCALL 326                        ; GrDrawBitmap DS:SI at AX,BX
 pop di
.bmskip:
 pop si
 jmp .next
; Bitmap at half resolution: every pixel is drawn 2x2. Each row is unpacked
; to DL_ROW, widened to a one row bitmap at DL_TMP and drawn twice.
.bm2:
 ; colour byte = scale (0/1: none): every pixel is drawn scale x scale.
 ; Each row is unpacked, widened to a one row bitmap and drawn.
 xor ah,ah
 mov al,dl
 cmp al,2
 jae .b2s1
 mov al,1
.b2s1:
 mov cx,ax                        ; scale
 mov ax,[si+2]
 mul cx
 add ax,bx
 cmp ax,VIEW_TOP                  ; above the page area?
 jle .b2out
 cmp bx,[H_VH]                    ; below the window?
 jge .b2out
 mov ax,[si]
 mul cx
 add ax,[V_RX0]
 jg .b2in                         ; else left of the window
.b2out:
 jmp .bmskip
.b2in:
 push bp
 push es
 mov bp,sp
 sub sp,28
 mov [bp-14],di                   ; gstate
 mov [bp-20],cx                   ; scale
 mov ax,ds
 mov es,ax
 mov ax,[si]
 mov [bp-6],ax                    ; w
 mov ax,[si+2]
 mov [bp-2],ax                    ; rows left
 mov [bp-8],bx                    ; y
 mov bx,DL_TMP                    ; one row bitmap: header at bx
 mov ax,DL_ROW                    ; packed row unpacked to ax
 cmp word [bp-20],1
 jne .b2hd
 mov bx,DL_ROW                    ; scale 1: unpack in place
 mov ax,DL_ROW+6
.b2hd:
 mov [bp-18],bx
 mov [bp-16],ax
 mov al,[si+5]
 mov [bx+5],al
 mov byte [bx+4],0                ; not compacted
 mov word [bx+2],1
 mov ax,[bp-6]
 mul word [bp-20]
 mov [bx],ax                      ; w * scale
 xor cx,cx                        ; mask bytes in / out
 xor dx,dx
 test byte [si+5],0x10
 jz .b2nm
 mov cx,[bp-6]
 add cx,7
 shr cx,1
 shr cx,1
 shr cx,1
 mov dx,ax
 add dx,7
 shr dx,1
 shr dx,1
 shr dx,1
.b2nm:
 mov [bp-10],cx
 mov [bp-22],dx
 mov ax,[bp-6]
 inc ax
 shr ax,1
 add ax,cx
 mov [bp-4],ax                    ; packed row length
 add si,6
.b2row:
 cmp word [bp-2],0
 jne .b2go
 jmp .b2end
.b2go:
 mov di,[bp-16]                   ; PackBits -> row buffer
.b2unp:
 mov ax,di
 sub ax,[bp-16]
 cmp ax,[bp-4]
 jae .b2unpd
 lodsb
 cmp al,128
 je .b2unp
 jb .b2lit
 mov cx,257
 xor ah,ah
 sub cx,ax
 lodsb
 rep stosb
 jmp .b2unp
.b2lit:
 xor ah,ah
 inc ax
 mov cx,ax
 rep movsb
 jmp .b2unp
.b2unpd:
 push si
 cmp word [bp-20],1
 je .b2one
 ; widen: clear the output row, then set mask bits and nibbles
 mov di,DL_TMP+6
 mov ax,[DL_TMP]                  ; w * scale
 inc ax
 shr ax,1
 add ax,[bp-22]
 mov cx,ax
 xor al,al
 rep stosb
 mov word [bp-24],0               ; x
 mov word [bp-26],0               ; X = x * scale
.b2px:
 mov ax,[bp-24]
 cmp ax,[bp-6]
 jb .b2px1
 jmp .b2pxd
.b2px1:
 mov bx,ax                        ; nibble of x
 shr bx,1
 add bx,[bp-10]
 mov dl,[DL_ROW+bx]
 test al,1
 jnz .b2lo
 mov cl,4
 shr dl,cl
.b2lo:
 and dl,0x0f
 mov dh,1                         ; visible (no mask)
 cmp word [bp-10],0
 je .b2vis
 mov bx,ax                        ; mask bit of x
 mov cl,3
 shr bx,cl
 mov cl,al
 and cl,7
 mov ch,0x80
 shr ch,cl
 xor dh,dh
 test [DL_ROW+bx],ch
 jz .b2vis
 mov dh,1
.b2vis:
 mov cx,[bp-20]                   ; scale copies
.b2cp:
 push cx
 mov ax,[bp-26]
 cmp word [bp-22],0
 je .b2nib
 or dh,dh
 jz .b2nib
 mov bx,ax                        ; output mask bit
 mov cl,3
 shr bx,cl
 mov cl,al
 and cl,7
 mov ch,0x80
 shr ch,cl
 or [DL_TMP+6+bx],ch
.b2nib:
 mov bx,ax                        ; output nibble
 shr bx,1
 add bx,[bp-22]
 mov ch,dl
 test al,1
 jnz .b2nlo
 mov cl,4
 shl ch,cl
.b2nlo:
 or [DL_TMP+6+bx],ch
 inc word [bp-26]
 pop cx
 loop .b2cp
 inc word [bp-24]
 jmp .b2px
.b2pxd:
 mov cx,[bp-20]                   ; draw scale rows
.b2dr:
 push cx
 mov bx,[bp-8]
 call .b2draw
 inc word [bp-8]
 pop cx
 loop .b2dr
 jmp .b2nx
.b2one:
 mov bx,[bp-8]
 call .b2draw
 inc word [bp-8]
.b2nx:
 pop si
 dec word [bp-2]
 jmp .b2row
.b2end:
 mov di,[bp-14]
 mov sp,bp
 pop es
 pop bp
 jmp .bmskip
.b2draw:                          ; one row of DL_TMP at y = bx
 cmp bx,[H_VH]
 jge .b2dx
 cmp bx,VIEW_TOP
 jl .b2dx
 mov si,[bp-18]
 mov ax,[V_RX0]
 xor dx,dx
 xor cx,cx
 mov di,[bp-14]
 KCALL 326                        ; GrDrawBitmap
.b2dx:
 ret
.rgbimg:                          ; scale, x, y, len, rows, then rows of runs
 lodsb
 xor ah,ah
 mov cx,ax                        ; scale
 lodsw
 sub ax,[H_SX]
 mov [V_RX0],ax
 lodsw
 sub ax,[H_SY]
 mov bx,ax
 lodsw
 mov dx,si
 add dx,ax
 push dx                          ; next record
 lodsw                            ; rows
 push bp
 push es
 mov bp,sp
 sub sp,12
 mov [bp-2],ax                    ; rows left
 mov [bp-4],bx                    ; y
 mov [bp-6],cx                    ; scale
 mov [bp-8],di                    ; gstate
 mov ax,ds
 mov es,ax
.rirow:
 cmp word [bp-2],0
 je .riend
 mov ax,[bp-4]
 cmp ax,[H_VH]                    ; below the window: done
 jge .riend
 lodsw
 mov [bp-10],ax                   ; runs
.rirun:
 cmp word [bp-10],0
 jne .rir1
 jmp .rinext
.rir1:
 dec word [bp-10]
 lodsw                            ; dx in pixels
 mul word [bp-6]
 add ax,[V_RX0]
 mov [bp-12],ax                   ; x of the run
 mov cx,[si]                      ; run width
 mov ax,cx
 shl ax,1
 add ax,cx
 add ax,6
 add ax,si
 push ax                          ; next run
 mov ax,[bp-4]
 add ax,[bp-6]
 cmp ax,VIEW_TOP                  ; row above the page area
 jle .rskip
 cmp word [bp-6],1
 jne .rexp
 mov bx,[bp-4]
 call .ridraw
 jmp .rskip
.rexp:                            ; enlarge: every pixel scale times
 mov di,DL_ROW
 mov ax,[si]
 mul word [bp-6]
 stosw
 mov ax,1
 stosw
 mov ax,0x0300                    ; not compacted, 24 bit
 stosw
 mov cx,[si]
 add si,6
.rexl:
 push cx
 mov cx,[bp-6]
.rexr:
 mov al,[si]
 stosb
 mov al,[si+1]
 stosb
 mov al,[si+2]
 stosb
 loop .rexr
 add si,3
 pop cx
 loop .rexl
 mov cx,[bp-6]
 mov bx,[bp-4]
.rexd:
 push cx
 push bx
 mov si,DL_ROW
 call .ridraw
 pop bx
 pop cx
 inc bx
 loop .rexd
.rskip:
 pop si
 jmp .rirun
.ridraw:                          ; DS:SI one row bitmap at y = bx
 cmp bx,[H_VH]
 jge .rdx
 cmp bx,VIEW_TOP
 jl .rdx
 mov ax,[bp-12]
 xor dx,dx
 xor cx,cx
 mov di,[bp-8]
 KCALL 326                        ; GrDrawBitmap
.rdx:
 ret
.rinext:
 dec word [bp-2]
 mov ax,[bp-6]
 add [bp-4],ax
 jmp .rirow
.riend:
 mov di,[bp-8]
 mov sp,bp
 pop es
 pop bp
 pop si
 jmp .next
.ext:                             ; records in an external image block
 lodsb                            ; block
 xor ah,ah
 mov bx,ax
 shl bx,1
 mov bx,[H_EXTS+bx]
 lodsw                            ; offset
 mov dx,ax
 lodsw                            ; length
 push si                          ; continue here
 or bx,bx
 jz .extx
 mov ax,bx
 push ax
 mov es,ax                        ; header and colour table into the block
 push di
 xor si,si
 xor di,di
 mov cx,DL_HDR/2
 rep movsw
 mov si,DL_CT
 mov di,DL_CT
 mov cx,DL_CTSZ/2
 rep movsw
 pop di
 mov si,dx
 add si,DL_HDR
 call .extrun                     ; back with DS = display list block
 pop ax
 mov bx,ds                        ; header back (colour and font caches)
 mov es,bx
 push ds
 mov ds,ax
 push di
 xor si,si
 xor di,di
 mov cx,DL_HDR/2
 rep movsw
 pop di
 pop ds
.extx:
 pop si
 jmp .next
.extrun:                          ; ends at its DL_END via .done (pop ds, ret)
 push ds
 mov ds,ax
 jmp .next
.rect:
 lodsb                            ; colour
 mov dl,al
 lodsw
 sub ax,[H_SX]
 mov [V_RX0],ax
 lodsw
 sub ax,[H_SY]
 mov bx,ax
 lodsw
 sub ax,[H_SX]
 inc ax
 mov cx,ax
 lodsw
 sub ax,[H_SY]
 inc ax
 ; cull rows outside the window
 cmp ax,VIEW_TOP
 jle .next
 cmp bx,[H_VH]
 jge .next
 cmp bx,VIEW_TOP
 jge .ytop
 mov bx,VIEW_TOP
.ytop:
 push si
 push ax
 push bx
 push cx
 cmp dl,[H_COL]
 je .samecol
 mov [H_COL],dl
 call col_regs
 push di
 KCALL 355                        ; GrSetAreaColor
 pop di
.samecol:
 pop cx
 pop bx
 pop dx
 mov ax,[V_RX0]
 push di
 KCALL 336                        ; GrFillRect
 pop di
 pop si
 jmp .next
.text:
 lodsb                            ; colour
 mov dl,al
 lodsw                            ; font
 mov [V_RFONT],ax
 lodsw                            ; size in quarter points
 mov [V_RSIZE],ax
 lodsb                            ; style
 mov [V_RSTYLE],al
 lodsw
 sub ax,[H_SX]
 mov [V_RX0],ax
 lodsw
 sub ax,[H_SY]
 mov [V_RY0],ax
 lodsb
 xor ah,ah
 mov [V_RLEN],ax
 ; cull: whole line above the bar or below the window
 mov bx,[V_RY0]
 cmp bx,[H_VH]
 jge .skiptext
 mov cx,[V_RSIZE]
 shr cx,1
 shr cx,1
 add bx,cx
 cmp bx,VIEW_TOP
 jle .skiptext
 push si
 cmp dl,[H_TCOL]
 je .tc
 mov [H_TCOL],dl
 push bx
 call col_regs
 push di
 KCALL 359                        ; GrSetTextColor
 pop di
 pop bx
 push di
 pop di
.tc:
 mov ax,[V_RFONT]
 cmp ax,[H_FONT]
 jne .setfont
 mov ax,[V_RSIZE]
 cmp ax,[H_SIZE]
 je .fontok
.setfont:
 mov ax,[V_RFONT]
 mov [H_FONT],ax
 mov ax,[V_RSIZE]
 mov [H_SIZE],ax
 mov dx,ax
 shr dx,1
 shr dx,1                         ; whole points
 and al,3
 ror al,1
 ror al,1                         ; quarter -> fraction (1/256 units)
 mov ah,al
 mov cx,[H_FONT]
 push di
 KCALL 366                        ; GrSetFont
 pop di
.fontok:
 mov al,[V_RSTYLE]
 xor ah,ah
 cmp ax,[H_STYLE]
 je .styleok
 mov [H_STYLE],ax
 mov ah,al
 not ah                           ; clear every bit not set
 push di
 KCALL 362                        ; GrSetTextStyle
 pop di
.styleok:
 pop si
 push si
 mov ax,[V_RX0]
 mov bx,[V_RY0]
 mov cx,[V_RLEN]
 push di
 KCALL 330                        ; GrDrawText DS:SI, CX chars
 pop di
 pop si
.skiptext:
 add si,[V_RLEN]
 jmp .next
.done:
 pop ds
 ret

; colour byte DL -> AX (and BX) for GrSetAreaColor / GrSetTextColor:
; 0..15 palette index, 16.. r,g,b from the true colour table (AH = 80h)
col_regs:
 mov al,dl
 xor ah,ah
 cmp al,16
 jb .ix
 push si
 sub al,16
 mov si,ax
 shl si,1
 shl si,1
 add si,DL_CT
 mov al,[si]
 mov bl,[si+1]
 mov bh,[si+2]
 mov ah,0x80
 pop si
.ix:
 ret


; ======================================================================
; keyboard
; ======================================================================
kbd_char:
 test dl,1                        ; CF_RELEASE: ignore key release
 jnz .pass
 call clr_prdiag
 cmp word [V_INIT],0
 je .super
 cmp word [V_MODE],0
 je .pagekeys
 cmp word [V_MODE],1
 je .listkeys
 call leave_page                  ; info/help: any key returns
 jmp .redraw
.listkeys:
 ; ---- file list keys ----
 cmp cl,0x0d                      ; Enter
 je .lopen
 cmp cl,0x1b                      ; Esc: back to the document
 je .lesc
 cmp cl,0x08                      ; Backspace: folder up
 je .lback
 cmp cl,'9'                       ; page up / down in the list
 je .lpg9
 cmp cl,'3'
 je .lpg3
 cmp cl,0x96
 je .lpg9
 cmp cl,0x97
 je .lpg3
 cmp ch,0xff                      ; cursor keys (and the mouse wheel)
 jne .lnc
 cmp cl,0x90
 je .lup
 cmp cl,0x91
 je .ldown
 cmp cl,0x94
 je .lhome
 cmp cl,0x95
 je .lend
 cmp cl,0x92
 je .done                         ; left / right: nothing in the list
 cmp cl,0x93
 je .done
.lnc:
 cmp cl,'8'
 je .lup
 cmp cl,'k'
 je .lup
 cmp cl,'2'
 je .ldown
 cmp cl,'j'
 je .ldown
 jmp .super
.lopen:
 call open_selected
 jmp .redraw
.lback:
 call go_parent
 jmp .redraw
.lesc:
 cmp word [V_FILE],0
 je .done
 mov word [V_MODE],0
 jmp .redraw
.lhome:
 xor ax,ax
 jmp short .lpgs
.lend:
 mov ax,[V_NPDF]
 or ax,ax
 jz .done
 dec ax
 jmp short .lpgs
.lpg9:
 mov ax,[V_LSEL]
 sub ax,16
 jge .lpgs
 xor ax,ax
 jmp short .lpgs
.lpg3:
 mov ax,[V_LSEL]
 add ax,16
 cmp ax,[V_NPDF]
 jb .lpgs
 mov ax,[V_NPDF]
 or ax,ax
 jz .done
 dec ax
.lpgs:
 mov [V_LSEL],ax
 jmp .redraw
.lup:
 cmp word [V_LSEL],0
 je .done
 dec word [V_LSEL]
 jmp .redraw
.ldown:
 mov ax,[V_LSEL]
 inc ax
 cmp ax,[V_NPDF]
 jae .done
 mov [V_LSEL],ax
 jmp .redraw
.pagekeys:
 cmp ch,0xff                      ; CS_CONTROL: cursor and paging keys
 jne .ascii
 cmp cl,0x90
 je .up
 cmp cl,0x91
 je .down
 cmp cl,0x92
 je .right
 cmp cl,0x93
 je .left
 cmp cl,0x96
 je .pgup
 cmp cl,0x97
 je .pgdn
 cmp cl,0x94
 je .home
 cmp cl,0x95
 je .end
 ; GeoWorks 1.28: numeric keypad arrives as CS_CONTROL + digit
 cmp cl,'9'
 je .pgup
 cmp cl,'3'
 je .pgdn
 cmp cl,'8'
 je .up
 cmp cl,'2'
 je .down
 cmp cl,'4'
 je .left
 cmp cl,'6'
 je .right
 cmp cl,'7'
 je .home
 cmp cl,'1'
 je .end
 cmp cl,'+'
 je .zin
 cmp cl,'-'
 je .zout
 jmp .super
.ascii:
 cmp cl,'e'                       ; true colour on / off
 jne .noe
 push ds
 mov ds,[V_DSEG]
 xor byte [H_TC],1
 pop ds
 jmp .newpage
.noe:
 cmp cl,'+'
 je .zin
 cmp cl,'='
 je .zin
 cmp cl,'-'
 je .zout
 cmp cl,'n'
 je .pgdn
 cmp cl,' '
 je .pgdn
 cmp cl,'p'
 je .pgup
 cmp cl,'b'
 je .pgup
 cmp cl,'j'
 je .down
 cmp cl,'k'
 je .up
 jmp .super
.up:
 sub word [V_SY],40
 jmp .redraw
.down:
 add word [V_SY],40
 jmp .redraw
.left:
 sub word [V_SX],40
 jmp .redraw
.right:
 add word [V_SX],40
 jmp .redraw
.pgdn:
 mov ax,[V_VH]
 sub ax,VIEW_TOP+20
 mov bx,[V_SY]
 add bx,[V_VH]
 sub bx,VIEW_TOP
 cmp bx,[V_PH]
 jl .scrolldown
 mov ax,[V_PAGE]
 inc ax
 cmp ax,[V_NPAGES]
 jge .done
 mov [V_PAGE],ax
 mov word [V_SY],0
 jmp .newpage
.scrolldown:
 add [V_SY],ax
 jmp .redraw
.pgup:
 cmp word [V_SY],0
 jle .prevpage
 mov ax,[V_VH]
 sub ax,VIEW_TOP+20
 sub [V_SY],ax
 jmp .redraw
.prevpage:
 mov ax,[V_PAGE]
 or ax,ax
 jz .done
 dec ax
 mov [V_PAGE],ax
 mov word [V_SY],0x7000           ; bottom, clamped later
 jmp .newpage
.home:
 mov word [V_PAGE],0
 mov word [V_SY],0
 jmp .newpage
.end:
 mov ax,[V_NPAGES]
 dec ax
 mov [V_PAGE],ax
 mov word [V_SY],0
 jmp .newpage
.zin:
 mov ax,[V_ZOOM]
 cmp ax,1024
 jae .done
 mov bx,ax
 shr bx,1
 shr bx,1
 add ax,bx                        ; * 1.25
 mov [V_ZOOM],ax
 jmp .newpage
.zout:
 mov ax,[V_ZOOM]
 cmp ax,64
 jbe .done
 mov bx,ax
 shr bx,1
 shr bx,1
 shr bx,1
 shr bx,1
 sub ax,bx
 shr bx,1
 sub ax,bx
 sub ax,bx                        ; ~ * 0.8
 mov [V_ZOOM],ax
.newpage:
 call render_page
.redraw:
 call repaint
.done:
 retf
.super:
.pass:
 mov di,PROCESS_CLASS
 KCALL 489
 retf


; ======================================================================
; status bar buttons and scroll bars (own drawing, clicks only)
; ======================================================================
; buttons at the right end of the status bar (left to right):
;   Erste  Zurueck  Weiter  Letzte    -  +
NBTN equ 6
btn_w:   dw 38, 46, 42, 40, 18, 18
btn_gap: dw 2, 2, 2, 10, 2, 0          ; space after each button
btn_lbl: dw s_b0, s_b1, s_b2, s_b3, s_b4, s_b5
s_b0: db 'Erste',0
s_b1: db 'Zur',0x9f,'ck',0             ; GEOS character set: 0x9F = u-umlaut
s_b2: db 'Weiter',0
s_b3: db 'Letzte',0
s_b4: db '-',0
s_b5: db '+',0

; AX = index -> AX = left x, BX = width
btn_x:
 push si
 push cx
 mov cx,ax
 ; total width of all buttons and gaps
 xor ax,ax
 xor si,si
.tot:
 cmp si,NBTN*2
 jae .t2
 add ax,[cs:btn_w+si]
 add ax,[cs:btn_gap+si]
 add si,2
 jmp .tot
.t2:
 mov bx,[V_FW]
 sub bx,ax
 sub bx,4                         ; right margin
 xor si,si
.pre:
 or cx,cx
 jz .got
 add bx,[cs:btn_w+si]
 add bx,[cs:btn_gap+si]
 add si,2
 dec cx
 jmp .pre
.got:
 mov ax,bx
 mov bx,[cs:btn_w+si]
 pop cx
 pop si
 ret

; AX = index -> CF set if the button would do nothing now (drawn greyed)
btn_off:
 cmp word [V_MODE],0
 jne .off                         ; list/info pages: page buttons do nothing
 cmp word [V_FILE],0
 je .off
 cmp ax,2
 jb .back
 cmp ax,4
 jb .fwd
 clc
 ret
.back:
 cmp word [V_PAGE],0
 je .off
 clc
 ret
.fwd:
 mov ax,[V_PAGE]
 inc ax
 cmp ax,[V_NPAGES]
 jae .off
 clc
 ret
.off:
 stc
 ret

buttons:
 xor si,si
.b:
 cmp si,NBTN
 jae .d
 mov ax,si
 call btn_x
 mov [B_L],ax
 add ax,bx
 mov [B_R],ax
 mov word [B_T],2
 mov word [B_B],BAR_H-2
 mov byte [B_SUNK],0
 push si
 call bevel
 pop si
 push si
 mov ax,si
 call btn_off
 mov al,0
 jnc .col
 mov al,8                         ; greyed
.col:
 call set_tcol
 ; centre the label roughly: (width - 6*len)/2
 mov bx,si
 shl bx,1
 mov si,[cs:btn_lbl+bx]
 xor cx,cx
.len:
 cmp byte [cs:si],0
 je .lend
 inc si
 inc cx
 jmp .len
.lend:
 mov si,[cs:btn_lbl+bx]
 mov ax,cx
 mov cx,6
 mul cx
 mov dx,[cs:btn_w+bx]
 sub dx,ax
 sar dx,1
 jns .pos
 xor dx,dx
.pos:
 mov ax,[B_L]
 add ax,dx
 inc ax
 push ds
 push cs
 pop ds
 mov bx,2
 call text_ds
 pop ds
 pop si
 inc si
 jmp .b
.d:
 ret

; ----------------------------------------------------------------------
; Motif style 3D box: [B_L],[B_T],[B_R],[B_B] (right/bottom exclusive),
; [B_SUNK] 0 = raised, 1 = sunken. Face light grey, edges white/dark grey.
; ----------------------------------------------------------------------
bevel:
 mov al,7
 call set_area
 mov ax,[B_L]
 mov bx,[B_T]
 mov cx,[B_R]
 mov dx,[B_B]
 call fill
 mov al,15                        ; light edge
 cmp byte [B_SUNK],0
 je .l1
 mov al,8
.l1:
 call set_area
 mov ax,[B_L]                     ; top
 mov bx,[B_T]
 mov cx,[B_R]
 mov dx,bx
 inc dx
 call fill
 mov ax,[B_L]                     ; left
 mov bx,[B_T]
 mov cx,ax
 inc cx
 mov dx,[B_B]
 call fill
 mov al,8                         ; dark edge
 cmp byte [B_SUNK],0
 je .l2
 mov al,15
.l2:
 call set_area
 mov ax,[B_L]                     ; bottom
 inc ax
 mov dx,[B_B]
 mov bx,dx
 dec bx
 mov cx,[B_R]
 call fill
 mov cx,[B_R]                     ; right
 mov ax,cx
 dec ax
 mov bx,[B_T]
 inc bx
 mov dx,[B_B]
 call fill
 ret

; raised button with a black triangle; AL = direction 0 up 1 down 2 left 3 right
arrow_btn:
 push ax
 mov byte [B_SUNK],0
 call bevel
 xor al,al
 call set_area
 pop ax
 mov [B_DIR],al
 ; centre
 mov ax,[B_L]
 add ax,[B_R]
 shr ax,1
 mov [B_CX],ax
 mov ax,[B_T]
 add ax,[B_B]
 shr ax,1
 mov [B_CY],ax
 xor si,si                        ; row 0..3, widths 1,3,5,7
.row:
 cmp si,4
 jae .done
 push si
 cmp byte [B_DIR],2
 jae .horiz
 ; vertical arrow: row y, span cx-si .. cx+si+1
 mov ax,[B_CX]
 sub ax,si
 mov cx,[B_CX]
 add cx,si
 inc cx
 mov bx,[B_CY]
 cmp byte [B_DIR],0
 jne .dn
 sub bx,2                         ; up: tip at top
 add bx,si
 jmp .vf
.dn:
 add bx,1
 sub bx,si
.vf:
 mov dx,bx
 inc dx
 call fill
 jmp .next
.horiz:
 mov bx,[B_CY]
 sub bx,si
 mov dx,[B_CY]
 add dx,si
 inc dx
 mov ax,[B_CX]
 cmp byte [B_DIR],2
 jne .rt
 sub ax,2                         ; left: tip at left
 add ax,si
 jmp .hf
.rt:
 add ax,1
 sub ax,si
.hf:
 mov cx,ax
 inc cx
 call fill
.next:
 pop si
 inc si
 jmp .row
.done:
 ret

; draws both bars; DI = gstate
scroll_bars:
 ; troughs (sunken)
 mov byte [B_SUNK],1
 mov ax,[V_VW]
 mov [B_L],ax
 mov ax,[V_FW]
 mov [B_R],ax
 mov word [B_T],VIEW_TOP
 mov ax,[V_VH]
 mov [B_B],ax
 call bevel
 mov byte [B_SUNK],1
 mov word [B_L],0
 mov ax,[V_VW]
 mov [B_R],ax
 mov ax,[V_VH]
 mov [B_T],ax
 mov ax,[V_FH]
 mov [B_B],ax
 call bevel
 ; corner
 mov al,7
 call set_area
 mov ax,[V_VW]
 mov bx,[V_VH]
 mov cx,[V_FW]
 mov dx,[V_FH]
 call fill
 ; arrow buttons
 mov ax,[V_VW]
 mov [B_L],ax
 mov ax,[V_FW]
 mov [B_R],ax
 mov word [B_T],VIEW_TOP
 mov word [B_B],VIEW_TOP+SB
 mov al,0
 call arrow_btn
 mov ax,[V_VW]
 mov [B_L],ax
 mov ax,[V_FW]
 mov [B_R],ax
 mov ax,[V_VH]
 mov [B_B],ax
 sub ax,SB
 mov [B_T],ax
 mov al,1
 call arrow_btn
 mov word [B_L],0
 mov word [B_R],SB
 mov ax,[V_VH]
 mov [B_T],ax
 mov ax,[V_FH]
 mov [B_B],ax
 mov al,2
 call arrow_btn
 mov ax,[V_VW]
 mov [B_R],ax
 sub ax,SB
 mov [B_L],ax
 mov ax,[V_VH]
 mov [B_T],ax
 mov ax,[V_FH]
 mov [B_B],ax
 mov al,3
 call arrow_btn
 ; vertical thumb
 mov ax,[V_VH]
 sub ax,VIEW_TOP+2*SB
 mov bx,[V_VH]
 sub bx,VIEW_TOP
 mov cx,[V_PH]
 mov dx,[V_SY]
 cmp word [V_MODE],1              ; file list: rows instead of pixels
 jne .vt
 push ax
 mov ax,[V_VH]
 sub ax,LIST_Y
 xor dx,dx
 mov cx,ROW_H
 div cx
 mov bx,ax                        ; visible rows
 pop ax
 mov cx,[V_NPDF]
 cmp cx,bx
 jae .vt1
 mov cx,bx
.vt1:
 mov dx,[V_LTOP]
.vt:
 call thumb
 add ax,VIEW_TOP+SB
 mov [B_T],ax
 add ax,[V_TMP]
 mov [B_B],ax
 mov ax,[V_VW]
 inc ax
 mov [B_L],ax
 mov ax,[V_FW]
 dec ax
 mov [B_R],ax
 mov byte [B_SUNK],0
 call bevel
 ; horizontal thumb
 mov ax,[V_VW]
 sub ax,2*SB
 mov bx,[V_VW]
 mov cx,[V_PW]
 mov dx,[V_SX]
 call thumb
 add ax,SB
 mov [B_L],ax
 add ax,[V_TMP]
 mov [B_R],ax
 mov ax,[V_VH]
 inc ax
 mov [B_T],ax
 mov ax,[V_FH]
 dec ax
 mov [B_B],ax
 mov byte [B_SUNK],0
 call bevel
 ret

; AX = track length, BX = visible, CX = content, DX = scroll
; -> AX = thumb start within track, [V_TMP] = thumb size
thumb:
 cmp cx,bx
 jg .scroll
 mov [V_TMP],ax                   ; everything visible: full track
 xor ax,ax
 ret
.scroll:
 push dx
 push cx
 push bx
 push ax
 mul bx                           ; track * visible
 div cx                           ; / content
 cmp ax,12
 jge .sz
 mov ax,12
.sz:
 mov [V_TMP],ax
 pop ax                           ; track
 sub ax,[V_TMP]                   ; free track
 pop bx                           ; visible
 pop cx                           ; content
 pop dx                           ; scroll
 sub cx,bx                        ; max scroll
 mul dx                           ; free * scroll
 div cx
 ret

; ----------------------------------------------------------------------
; mouse: CX = x, DX = y. Returns AX = MRF_PROCESSED.
; ----------------------------------------------------------------------
mouse_select:
 cmp word [V_INIT],0
 je .done
 call clr_prdiag
 mov es,[V_LSEG]                  ; document -> window coordinates
 sub cx,[es:L_OX]
 sub dx,[es:L_OY]
 cmp dx,BAR_H
 jge .notbar
 ; status bar buttons
 xor si,si
.btn:
 cmp si,NBTN
 jae .done
 push cx
 mov ax,si
 call btn_x
 pop cx
 cmp cx,ax
 jl .nextbtn
 add ax,bx
 cmp cx,ax
 jge .nextbtn
 mov ax,si
 push si
 call btn_off
 pop si
 jc .done
 cmp si,0
 je .first
 cmp si,1
 je .prev
 cmp si,2
 je .next
 cmp si,3
 je .last
 cmp si,4
 je .zout
 jmp .zin
.nextbtn:
 inc si
 jmp .btn
.first:
 cmp word [V_PAGE],0
 je .done
 mov word [V_PAGE],0
 mov word [V_SY],0
 jmp .new
.last:
 mov ax,[V_NPAGES]
 dec ax
 cmp ax,[V_PAGE]
 je .done
 mov [V_PAGE],ax
 mov word [V_SY],0
 jmp .new
.notbar:
 cmp word [V_MODE],0
 je .pageclick
 cmp word [V_MODE],1
 je .listclick
 call leave_page                  ; info/help: click returns
 jmp .redraw
.listclick:
 ; file list: scroll bar moves the selection, click on a row opens it
 cmp cx,[V_VW]
 jl .lrow
 cmp word [V_NPDF],0
 je .done
 mov ax,[V_VH]
 sub ax,LIST_Y
 xor bx,bx
 push dx
 xor dx,dx
 mov cx,ROW_H
 div cx
 pop dx
 dec ax                           ; rows per page - 1
 jg .lr
 mov ax,1
.lr:
 cmp dx,VIEW_TOP+SB
 jl .lpgup
 mov bx,[V_VH]
 sub bx,SB
 cmp dx,bx
 jge .lpgdn
 ; track: selection proportional to the position
 sub dx,VIEW_TOP+SB
 mov ax,[V_NPDF]
 mul dx
 mov cx,[V_VH]
 sub cx,VIEW_TOP+2*SB
 jle .done
 div cx
 jmp .lset
.lpgup:
 mov bx,[V_LSEL]
 sub bx,ax
 jge .lset2
 xor bx,bx
.lset2:
 mov ax,bx
 jmp .lset
.lpgdn:
 add ax,[V_LSEL]
.lset:
 cmp ax,[V_NPDF]
 jb .lset3
 mov ax,[V_NPDF]
 dec ax
.lset3:
 mov [V_LSEL],ax
 jmp .redraw
.lrow:
 cmp dx,LIST_Y
 jl .done
 mov ax,dx
 sub ax,LIST_Y
 xor dx,dx
 mov cx,ROW_H
 div cx
 add ax,[V_LTOP]
 cmp ax,[V_NPDF]
 jae .done
 mov [V_LSEL],ax
 call open_selected
 jmp .redraw
.pageclick:
 cmp cx,[V_VW]
 jl .notv
 cmp dx,[V_VH]
 jge .done
 ; vertical bar
 cmp dx,VIEW_TOP+SB
 jl .up
 mov ax,[V_VH]
 sub ax,SB
 cmp dx,ax
 jge .down
 ; jump: sy = (y - tracktop) * (PH - visible) / track
 sub dx,VIEW_TOP+SB
 mov ax,[V_PH]
 mov bx,[V_VH]
 sub bx,VIEW_TOP
 sub ax,bx
 jle .done
 mul dx
 mov cx,[V_VH]
 sub cx,VIEW_TOP+2*SB
 div cx
 mov [V_SY],ax
 jmp .redraw
.notv:
 cmp dx,[V_VH]
 jl .done                         ; click on the page itself: nothing yet
 cmp cx,SB
 jl .left
 mov ax,[V_VW]
 sub ax,SB
 cmp cx,ax
 jge .right
 sub cx,SB
 mov ax,[V_PW]
 sub ax,[V_VW]
 jle .done
 mul cx
 mov cx,[V_VW]
 sub cx,2*SB
 div cx
 mov [V_SX],ax
 jmp .redraw
.up:
 sub word [V_SY],40
 jmp .redraw
.down:
 add word [V_SY],40
 jmp .redraw
.left:
 sub word [V_SX],40
 jmp .redraw
.right:
 add word [V_SX],40
 jmp .redraw
.prev:
 mov ax,[V_PAGE]
 or ax,ax
 jz .done
 dec ax
 mov [V_PAGE],ax
 mov word [V_SY],0
 jmp .new
.next:
 mov ax,[V_PAGE]
 inc ax
 cmp ax,[V_NPAGES]
 jge .done
 mov [V_PAGE],ax
 mov word [V_SY],0
 jmp .new
.zin:
 call zoom_in
 jmp .new
.zout:
 call zoom_out
.new:
 call render_page
.redraw:
 call repaint
.done:
 mov ax,0x8000                    ; MRF_PROCESSED
 retf

zoom_in:
 mov ax,[V_ZOOM]
 cmp ax,1024
 jae .n
 mov bx,ax
 shr bx,1
 shr bx,1
 add ax,bx
 mov [V_ZOOM],ax
.n:
 ret
zoom_out:
 mov ax,[V_ZOOM]
 cmp ax,64
 jbe .n
 mov bx,ax
 shr bx,1
 shr bx,1
 shr bx,1
 shr bx,1
 sub ax,bx
 shr bx,1
 sub ax,bx
 sub ax,bx
 mov [V_ZOOM],ax
.n:
 ret

; ======================================================================
; file list (FileEnum, GeoWorks 1.28 kernel ordinal 65)
; ======================================================================
; FileEnum parameter block on the stack (layout read from GeoManager's
; calls; FileEnum removes the 0x30 bytes itself):
;   +0 byte search flags (0x40 = non-GEOS files)  +1 word 0x4000  +3 byte 0x20
;   +4 fptr directory path   +8 word disk handle
;   +12h fptr result buffer  +16h word max entries   +2Ah word skip count
; returns CF = error, BX = entries stored (ENT_SIZE bytes each), DX = more

; CF set on failure. Fills the list block.
enum_docs:
 push ds
 mov es,[V_LSEG]
 ; current directory and its disk
 mov ds,[V_LSEG]
 mov si,L_CUR
 mov byte [si],0
 mov cx,120
 KCALL 75                         ; FileGetCurrentPath DS:SI, CX -> BX disk
 pop ds
 jnc .gotcur
 stc
 ret
.gotcur:
 mov [V_DISK],bx
 ; DOCUMENT is looked for next to the start directory and up to two
 ; levels above it (start dir is WORLD, SYSAPPL, WORLD\EXTRAS, ...)
 mov word [V_NENT],0
 mov word [V_NPDF],0
 mov word [V_LSEL],0
 mov word [V_LTOP],0
 xor cx,cx                        ; levels to strip
.level:
 push cx
 call build_docdir
 mov es,[V_LSEG]
 mov word [es:L_SKIP],0
 call file_enum
 pop cx
 jnc .ok
 inc cx
 cmp cx,3
 jb .level
 stc
 ret
.ok:
 mov es,[V_LSEG]
 mov byte [es:L_DIRSET],1
 ; fall through: list the folder

; ----------------------------------------------------------------------
; list L_DIR: "[..]" first, then the folders, then the PDF files, each
; group sorted by name. CF = folder could not be read
; ----------------------------------------------------------------------
list_dir:
 mov word [V_NPDF],0
 mov word [V_LSEL],0
 mov word [V_LTOP],0
 ; parent entry unless L_DIR is the root
 mov es,[V_LSEG]
 mov si,L_DIR
.ln:
 cmp byte [es:si],0
 je .le
 inc si
 jmp .ln
.le:
 sub si,L_DIR
 cmp si,1
 jbe .noparent
 mov di,L_NAMES
 mov word [es:di],'[.'
 mov word [es:di+2],'.]'
 mov byte [es:di+4],0
 mov byte [es:di+NAME_TY],2
 mov word [V_NPDF],1
.noparent:
 mov al,1
 call enum_pass
 jc .r
 mov al,2
 call enum_pass
.r:
 ret

; AL = pass (1 folders, 2 PDFs): enumerate all of L_DIR, append, sort
enum_pass:
 mov es,[V_LSEG]
 mov [es:L_PASS],al
 push word [V_NPDF]               ; first row of this group
 mov word [es:L_SKIP],0
 call file_enum
 jc .err
.more:
 mov [V_NENT],bx
 push dx
 call collect_pdfs
 pop dx
 or dx,dx                         ; more entries than fitted?
 jz .sort
 mov es,[V_LSEG]
 mov ax,[es:L_SKIP]
 add ax,[V_NENT]
 mov [es:L_SKIP],ax
 call file_enum
 jnc .more
.sort:
 pop bx
 mov dx,[V_NPDF]
 call sort_rows
 clc
 ret
.err:
 pop bx
 stc
 ret

; sort list rows BX .. DX-1 by name (simple exchange sort, at most 100)
sort_rows:
 push ds
 mov ds,[V_LSEG]
 push ds
 pop es
.outer:
 mov ax,bx
 inc ax
 cmp ax,dx
 jae .done
.inner:
 cmp ax,dx
 jae .next
 ; compare row BX with row AX
 push ax
 push bx
 push dx
 mov cl,4
 shl ax,cl
 shl bx,cl
 add ax,L_NAMES
 add bx,L_NAMES
 mov si,bx
 mov di,ax
.cmp:
 mov cl,[si]
 cmp cl,[di]
 ja .swap
 jb .keep
 or cl,cl
 jz .keep
 inc si
 inc di
 jmp .cmp
.swap:
 mov si,bx
 mov di,ax
 mov cx,NAME_SZ/2
.sw:
 mov ax,[si]
 xchg ax,[di]
 mov [si],ax
 inc si
 inc si
 inc di
 inc di
 loop .sw
.keep:
 pop dx
 pop bx
 pop ax
 inc ax
 jmp .inner
.next:
 inc bx
 jmp .outer
.done:
 pop ds
 ret

; L_DIR = current path with CX trailing components removed + "\DOCUMENT"
build_docdir:
 push ds
 mov ds,[V_LSEG]
 push ds
 pop es
 mov si,L_CUR
 mov di,L_DIR
.cp:
 lodsb
 stosb
 or al,al
 jnz .cp
 dec di                           ; DI -> NUL
.strip:
 jcxz .trail
.back:
 cmp di,L_DIR
 je .trail
 dec di
 cmp byte [di],'\'
 jne .back
 dec cx
 jmp .strip
.trail:
 cmp di,L_DIR
 je .app
 cmp byte [di-1],'\'
 jne .app
 dec di
.app:
 mov si,s_docdir
.cp2:
 mov al,[cs:si]
 stosb
 inc si
 or al,al
 jnz .cp2
 pop ds
 ret

; FileEnum of L_DIR into L_ENT. CF = error, BX = entries
file_enum:
 push bp
 sub sp,0x30
 mov bp,sp
 push ss
 pop es
 mov di,sp
 xor ax,ax
 mov cx,0x18
 rep stosw                        ; clean parameter block
 mov byte [bp+0],0xfc             ; all files (0x40 alone = directories only)
 mov word [bp+1],0x4000
 mov byte [bp+3],0x20
 mov word [bp+4],L_DIR
 mov ax,[V_LSEG]
 mov [bp+6],ax
 mov ax,[V_DISK]
 mov [bp+8],ax
 mov word [bp+0x12],L_ENT
 mov ax,[V_LSEG]
 mov [bp+0x14],ax
 mov word [bp+0x16],MAX_ENT
 mov es,[V_LSEG]
 mov ax,[es:L_SKIP]
 mov [bp+0x2a],ax                 ; entries to skip
 KCALL 65                         ; FileEnum (pops the block)
 pop bp
 ret

; AX = entry index, DS = list block -> AX = offset of its name
name_ofs:
 push bx
 push cx
 push dx
 mov cx,ENT_SIZE
 mul cx
 add ax,L_ENT+ENT_NAME
 mov bx,ax
 cmp byte [bx],0
 jne .k
 inc ax
.k:
 pop dx
 pop cx
 pop bx
 ret

; append the FileEnum entries of this pass (L_PASS) to the list rows
collect_pdfs:
 push ds
 mov cx,[V_NENT]
 cmp cx,MAX_ENT
 jbe .c1
 xor cx,cx
.c1:
 mov dx,[V_NPDF]
 mov ds,[V_LSEG]
 push ds
 pop es
 xor si,si                        ; entry index
.e:
 cmp si,cx
 jae .d
 cmp dx,MAX_PDF
 jae .d
 push si
 push cx
 mov ax,si
 call name_ofs                    ; AX -> name
 mov bx,ax
 cmp byte [bx],'.'                ; "." and ".." from DOS
 je .no
 cmp byte [bx],0
 je .no
 mov ax,si
 push dx
 mov cx,ENT_SIZE
 mul cx
 pop dx
 mov di,ax
 xor ax,ax                        ; AL = 1 for a folder
 test byte [L_ENT+di],0x10
 jz .f
 inc al
.f:
 xor di,di                        ; length up to 12 characters (8.3)
.n:
 cmp di,12
 jae .end
 cmp byte [bx+di],0
 je .end
 inc di
 jmp .n
.end:
 cmp byte [L_PASS],1
 jne .pdf
 or al,al
 jz .no
 jmp .take
.pdf:
 or al,al
 jnz .no
 cmp di,5
 jb .no
 mov ax,[bx+di-4]
 and ah,0xdf
 cmp ax,'.P'
 jne .no
 mov ax,[bx+di-2]
 and ax,0xdfdf
 cmp ax,'DF'
 jne .no
 xor al,al
.take:
 ; copy name to L_NAMES + dx*NAME_SZ, folders as "[NAME]"
 push ax
 mov cx,di                        ; length
 mov ax,dx
 push cx
 mov cl,4
 shl ax,cl
 pop cx
 add ax,L_NAMES
 mov di,ax
 pop ax
 mov [di+NAME_TY],al
 or al,al
 jz .cp
 mov byte [di],'['
 inc di
.cp:
 mov si,bx
 rep movsb
 or al,al
 jz .z
 mov byte [di],']'
 inc di
.z:
 mov byte [di],0
 inc dx
.no:
 pop cx
 pop si
 inc si
 jmp .e
.d:
 pop ds
 mov [V_NPDF],dx
 ret

; diagnostic line: disk, entries, first entry as hex
make_diag:
 push ds
 mov ax,[V_DISK]
 mov bx,[V_NENT]
 mov es,[V_LSEG]
 mov di,L_DIAG
 mov si,s_dg1
 call copy_cs
 push ax
 mov al,ah
 call put_hex
 pop ax
 call put_hex
 mov si,s_dg2
 call copy_cs
 mov al,bh
 call put_hex
 mov al,bl
 call put_hex
 mov si,s_dg3
 call copy_cs
 mov ds,[V_LSEG]
 xor ax,ax
 call name_ofs
 mov si,ax
 mov cx,13
.nm:
 lodsb
 or al,al
 jz .nme
 stosb
 loop .nm
.nme:
 mov al,' '
 stosb
 stosb
 mov si,L_ENT
 mov cx,ENT_SIZE
.h:
 lodsb
 call put_hex
 mov al,' '
 stosb
 loop .h
 mov byte [es:di],0
 pop ds
 ret
s_docdir: db '\DOCUMENT',0
s_dg1: db 'Disk ',0
s_dg2: db '  Eintraege ',0
s_dg3: db '  #1: ',0

; ----------------------------------------------------------------------
; drawing of the list (DI = gstate)
; ----------------------------------------------------------------------
paint_list:
 mov al,15
 call set_area
 mov ax,8
 mov bx,VIEW_TOP+8
 mov cx,[V_VW]
 sub cx,8
 mov dx,[V_VH]
 sub dx,8
 call fill
 xor ax,ax
 call set_tcol
 push di
 mov cx,0x1200
 mov dx,12
 xor ah,ah
 KCALL 366                        ; GrSetFont URW Sans 12
 pop di
 push ds                          ; title
 push cs
 pop ds
 mov si,s_ltitle
 mov ax,16
 mov bx,VIEW_TOP+12
 call text_ds
 pop ds
 push ds
 mov ds,[V_LSEG]
 mov si,L_DIR
 mov ax,80
 mov bx,VIEW_TOP+12
 call text_ds
 pop ds
 push di                          ; small font for the diagnostic line
 mov cx,0x1200
 mov dx,9
 xor ah,ah
 KCALL 366
 pop di
 mov al,8
 call set_tcol
 push ds
 mov es,[V_LSEG]
 cmp byte [es:L_SHOWD],0
 je .hint
 mov ds,[V_LSEG]
 mov si,L_DIAG
 jmp .hd
.hint:
 push cs
 pop ds
 mov si,s_lhint
.hd:
 mov ax,16
 mov bx,VIEW_TOP+32
 call text_ds
 pop ds
 push di
 mov cx,0x1200
 mov dx,12
 xor ah,ah
 KCALL 366
 pop di
 cmp word [V_NPDF],0
 jne .rows
 xor ax,ax
 call set_tcol
 push ds
 push cs
 pop ds
 mov si,s_none
 mov ax,16
 mov bx,LIST_Y
 call text_ds
 pop ds
 ret
.rows:
 call list_clamp
 mov si,[V_LTOP]
.r:
 cmp si,[V_NPDF]
 jae .done
 mov ax,si
 sub ax,[V_LTOP]
 mov cx,ROW_H
 mul cx
 add ax,LIST_Y
 mov bx,ax                        ; row top
 mov dx,bx
 add dx,ROW_H
 add dx,ROW_H
 cmp dx,[V_VH]
 ja .done
 push si
 push bx
 cmp si,[V_LSEL]
 jne .plain
 mov al,1                         ; selected: dark blue bar, white text
 call set_area
 pop bx
 push bx
 mov ax,12
 mov cx,[V_VW]
 sub cx,12
 mov dx,bx
 add dx,ROW_H
 call fill
 mov al,15
 call set_tcol
 jmp .name
.plain:
 xor ax,ax
 call set_tcol
.name:
 pop bx
 pop si
 push si
 push ds
 mov ds,[V_LSEG]
 mov ax,si
 mov cl,4
 shl ax,cl
 add ax,L_NAMES
 mov si,ax
 mov ax,20
 call text_ds                     ; NUL terminated 8.3 name or [FOLDER]
 pop ds
 pop si
 inc si
 jmp .r
.done:
 ret
s_ltitle: db 'Ordner:',0
s_none:   db 'Keine Ordner und PDF-Dateien gefunden.',0
s_lhint:  db 'Klick oder Eingabe ',0x9a,'ffnet  -  [..] oder R',0x9f,'cktaste: Ordner h',0x9a,'her  -  Esc: zur',0x9f,'ck',0

; keep the selection visible
list_clamp:
 mov ax,[V_VH]
 sub ax,LIST_Y+ROW_H
 xor dx,dx
 mov cx,ROW_H
 div cx                           ; visible rows
 or ax,ax
 jnz .v
 inc ax
.v:
 mov bx,[V_LSEL]
 cmp bx,[V_LTOP]
 jae .t1
 mov [V_LTOP],bx
.t1:
 mov cx,[V_LTOP]
 add cx,ax
 cmp bx,cx
 jb .t2
 sub bx,ax
 inc bx
 mov [V_LTOP],bx
.t2:
 ret

; open the selected list entry: PDF, folder or parent folder
open_selected:
 cmp word [V_NPDF],0
 je .no
 mov ax,[V_LSEL]
 mov cl,4
 shl ax,cl
 add ax,L_NAMES
 mov bx,ax                        ; row
 mov es,[V_LSEG]
 mov al,[es:bx+NAME_TY]
 cmp al,1
 je .folder
 cmp al,2
 je .parent
 push ds
 mov ds,[V_LSEG]
 mov si,L_DIR
 mov di,L_FULL
.c1:
 lodsb
 stosb
 or al,al
 jnz .c1
 dec di
 cmp byte [di-1],'\'             ; root "\" already ends in a backslash
 je .c2
 mov al,'\'
 stosb
.c2:
 mov si,bx
.c3:
 lodsb
 stosb
 or al,al
 jnz .c3
 pop ds
 ; close the previous document
 mov bx,[V_FILE]
 or bx,bx
 jz .open
 mov al,0
 KCALL 54                         ; FileClose
 mov word [V_FILE],0
.open:
 push ds
 mov ds,[V_LSEG]
 mov dx,L_FULL
 mov al,0x40                      ; FILE_DENY_NONE | FILE_ACCESS_R
 KCALL 52                         ; FileOpen
 pop ds
 jc .err
 mov word [V_MODE],0
 call open_doc
.no:
 ret
.err:
 mov word [V_MODE],0
 mov word [V_NPAGES],0
 mov ax,-1
 call set_error
 ret
.folder:
 ; append "\NAME" (row text without the brackets) to L_DIR
 mov di,L_DIR
.f1:
 cmp byte [es:di],0
 je .f2
 inc di
 jmp .f1
.f2:
 cmp di,L_DIR+110                 ; keep room in the 128 byte path
 ja .no
 mov dx,di                        ; old end, to undo on error
 cmp byte [es:di-1],'\'
 je .f3
 mov byte [es:di],'\'
 inc di
.f3:
 inc bx                           ; skip '['
.f4:
 mov al,[es:bx]
 cmp al,']'
 je .f5
 or al,al
 je .f5
 mov [es:di],al
 inc di
 inc bx
 jmp .f4
.f5:
 mov byte [es:di],0
 push dx
 call list_dir
 pop dx
 jnc .no
 mov es,[V_LSEG]                  ; unreadable: back to where we were
 mov di,dx
 mov byte [es:di],0
 call list_dir
 ret
.parent:
 jmp go_parent

; L_DIR one level up (not above the root) and list it
go_parent:
 mov es,[V_LSEG]
 mov di,L_DIR
 xor bx,bx                        ; last backslash
.p1:
 mov al,[es:di]
 or al,al
 jz .p2
 cmp al,'\'
 jne .p3
 mov bx,di
.p3:
 inc di
 jmp .p1
.p2:
 or bx,bx
 jnz .p5
 cmp byte [es:L_DIR],0            ; relative path without backslash:
 je .r                            ; go to the root
 mov word [es:L_DIR],'\\'
 mov byte [es:L_DIR+1],0
 jmp .p6
.p5:
 cmp bx,L_DIR                     ; "\NAME" -> "\"
 jne .p4
 cmp byte [es:L_DIR+1],0
 je .r                            ; already the root
 inc bx
.p4:
 mov byte [es:bx],0
.p6:
 call list_dir
.r:
 ret

; ======================================================================
; menu "Datei" (GenTriggers send these messages to the process)
; ======================================================================
menu_open:
 cmp word [V_INIT],0
 je .r
 call relist
 mov word [V_MODE],1
 call repaint
.r:
 retf

; list the last folder again, or look for DOCUMENT the first time
relist:
 mov es,[V_LSEG]
 cmp byte [es:L_DIRSET],0
 je .first
 call list_dir
 jnc .r
.first:
 call enum_docs
.r:
 ret

menu_close:
 cmp word [V_INIT],0
 je .r
 mov bx,[V_FILE]
 or bx,bx
 jz .nf
 mov al,0
 KCALL 54                         ; FileClose
 mov word [V_FILE],0
.nf:
 mov word [V_NPAGES],0
 mov word [V_ERR],0
 call relist
 mov word [V_MODE],1
 call repaint
.r:
 retf

; ----------------------------------------------------------------------
; printing: the PrintControl (spool library, like in the Notizblock) sends
; this after its dialog. CX:DX = PrintControl, BP = GString to draw into.
; The current page is drawn fitted into the printable area.
; ----------------------------------------------------------------------
menu_print:
 mov es,[V_LSEG]
 mov [es:L_PCH],cx
 mov [es:L_PCC],dx
 mov [es:L_PGS],bp
 mov di,L_DIAG
 mov si,s_prd0
 call copy_cs
 mov byte [es:di],0
 mov byte [es:L_PRDIAG],1
 cmp word [V_FILE],0
 je .done
 cmp word [V_NPAGES],0
 je .done
 mov ax,0x6016                    ; margins -> AX left, CX top, DX right, BP bottom
 call pc_call
 mov es,[V_LSEG]
 mov [es:L_ML],ax
 mov [es:L_MT],cx
 add dx,ax
 add bp,cx
 push dx                          ; left + right
 push bp                          ; top + bottom
 mov ax,0x6012                    ; paper size -> CX, DX
 call pc_call
 pop bp
 pop bx
 mov es,[V_LSEG]
 mov [es:L_PPW],cx
 mov [es:L_PPH],dx
 push cx
 push dx
 sub cx,bx
 sub dx,bp
 mov [es:L_PRW],cx
 mov [es:L_PRH],dx
 ; all pages one below the other: document = N sheets
 mov ax,[V_NPAGES]
 cmp ax,MAX_PRINT
 jbe .np
 mov ax,MAX_PRINT
.np:
 mov [es:L_PN],ax
 pop dx
 push dx
 mul dx                           ; height * pages
 mov dx,ax
 pop ax
 pop cx
 push ax
 mov ax,0x6009                    ; document size
 call pc_call
 pop ax
 ; keep the screen state
 mov es,[V_LSEG]
 mov ax,[V_ZOOM]
 mov [es:L_SAVE],ax
 push es
 mov es,[V_DSEG]
 mov byte [es:H_PRN],1
 pop es
 mov ax,[V_SX]
 mov [es:L_SAVE+2],ax
 mov ax,[V_SY]
 mov [es:L_SAVE+4],ax
 mov ax,[V_VH]
 mov [es:L_SAVE+6],ax
 mov ax,[V_PAGE]
 mov [es:L_SAVE+8],ax
 ; origin: printable area of sheet 1; replay adds VIEW_TOP to y
 mov di,[es:L_PGS]
 mov dx,[es:L_ML]
 xor cx,cx
 mov bx,[es:L_MT]
 sub bx,VIEW_TOP
 xor ax,ax
 KCALL 310                        ; GrApplyTranslation
 mov es,[V_LSEG]
 mov word [es:L_PK],0
.page:
 mov es,[V_LSEG]
 mov ax,[es:L_PK]
 mov [V_PAGE],ax
 call page_points
 mov es,[V_LSEG]
 mov ax,[es:L_PRW]
 call fit_zoom_w
 push ax
 mov es,[V_LSEG]
 mov ax,[es:L_PRH]
 call fit_zoom_h
 pop bx
 cmp ax,bx
 jbe .z
 mov ax,bx
.z:
 cmp ax,16
 jae .z2
 mov ax,16
.z2:
 mov [V_ZOOM],ax
 call render_page
 cmp word [V_ERR],0
 jne .skip
 mov word [V_SX],0
 mov word [V_SY],0
 mov word [V_VH],32000
 mov es,[V_LSEG]
 mov di,[es:L_PGS]
 call replay
.skip:
 mov word [V_ERR],0
 mov es,[V_LSEG]
 inc word [es:L_PK]
 mov ax,[es:L_PK]
 cmp ax,[es:L_PN]
 jae .pages_done
 mov di,[es:L_PGS]                ; next sheet: one paper height down
 xor dx,dx
 xor cx,cx
 mov bx,[es:L_PPH]
 xor ax,ax
 KCALL 310
 jmp .page
.pages_done:
 mov es,[V_LSEG]
 mov di,L_DIAG
 mov si,s_prd1
 call copy_cs
 mov ax,[es:L_PN]
 call put_num
 mov si,s_prd2
 call copy_cs
 mov byte [es:di],0
 mov byte [es:L_PRDIAG],1
.restore:
 mov es,[V_LSEG]
 mov ax,[es:L_SAVE]
 mov [V_ZOOM],ax
 mov ax,[es:L_SAVE+2]
 mov [V_SX],ax
 mov ax,[es:L_SAVE+4]
 mov [V_SY],ax
 mov ax,[es:L_SAVE+6]
 mov [V_VH],ax
 mov ax,[es:L_SAVE+8]
 mov [V_PAGE],ax
 mov es,[V_DSEG]
 mov byte [es:H_PRN],0
 call page_points
 call render_page                 ; screen display list again
.done:
 mov ax,0x25a                     ; printing done
 call pc_call
 call repaint                     ; show the result in the status bar
 retf

s_prd0: db 'Kein Dokument zum Drucken',0
s_prd1: db 'Gedruckt: ',0
s_prd2: db ' Seite(n)',0

clr_prdiag:
 push es
 mov es,[V_LSEG]
 mov byte [es:L_PRDIAG],0
 pop es
 ret

; ----------------------------------------------------------------------
; the PrintControl asks its document object (us) for the document name:
; CX:DX = PrintControl, BP = reply message; reply with CX:DX = name
; ----------------------------------------------------------------------
print_docname:
 mov es,[V_LSEG]
 mov byte [es:L_NAMEOK],1
 mov [es:L_PCH],cx
 mov [es:L_PCC],dx
 push bp
 mov di,L_DOCN
 mov si,s_docname
 call copy_cs
 mov byte [es:di],0
 pop ax                           ; reply message
 mov cx,[V_LSEG]
 mov dx,L_DOCN
 call pc_call
 retf

s_docname: db 'PDF-Dokument',0

; tell the PrintControl (interface block, chunk PC_CHUNK) the page range
; 1..n of the document, so its dialog offers all pages
set_print_range:
 push si
 push di
 mov bx,3
 KCALL 150                        ; GeodeGetResourceHandle -> BX
 mov si,PC_CHUNK
 mov cx,1
 mov dx,[V_NPAGES]
 cmp dx,MAX_PRINT
 jbe .r
 mov dx,MAX_PRINT
.r:
 mov ax,0x6005
 xor di,di                        ; send, no reply needed
 KCALL 169                        ; ObjMessage
 pop di
 pop si
 ret

; ObjMessage (call) to the PrintControl: AX = message, CX DX BP = arguments
pc_call:
 push si
 push di
 mov es,[V_LSEG]
 mov bx,[es:L_PCH]
 mov si,[es:L_PCC]
 mov di,0x8080
 KCALL 169                        ; ObjMessage
 pop di
 pop si
 ret

; AX = printable width/height in points -> zoom (256 = 1:1) for V_PTW / V_PTH
fit_zoom_w:
 mov bx,[V_PTW]
 jmp short fit_zoom
fit_zoom_h:
 mov bx,[V_PTH]
fit_zoom:
 or bx,bx
 jz .one
 mov dx,ax
 mov al,0
 mov ah,dl
 mov dl,dh
 xor dh,dh                        ; DX:AX = AX * 256
 cmp dx,bx
 jae .big
 div bx
 ret
.big:
 mov ax,0xffff
 ret
.one:
 mov ax,256
 ret

menu_info:
 mov ax,2
 jmp short show_text
menu_help:
 mov ax,3
show_text:
 cmp word [V_INIT],0
 je .r
 mov [V_MODE],ax
 call repaint
.r:
 retf

; info/help page left: back to the document, else to the list
leave_page:
 mov word [V_MODE],0
 cmp word [V_FILE],0
 jne .r
 mov word [V_MODE],1
.r:
 ret

; info (mode 2) or help (mode 3) text page; DI = gstate
paint_text:
 mov al,15
 call set_area
 mov ax,8
 mov bx,VIEW_TOP+8
 mov cx,[V_VW]
 sub cx,8
 mov dx,[V_VH]
 sub dx,8
 call fill
 xor ax,ax
 call set_tcol
 mov si,t_info
 cmp word [V_MODE],2
 je .go
 mov si,t_help
.go:
 mov bx,VIEW_TOP+14
.line:
 mov al,[cs:si]                   ; font size byte, 0 = end
 or al,al
 jz .done
 inc si
 xor ah,ah
 push bx
 push si
 push di
 mov dx,ax
 mov cx,0x1200
 xor ah,ah
 KCALL 366                        ; GrSetFont URW Sans
 pop di
 pop si
 pop bx
 push bx
 push si
 push ds
 push cs
 pop ds
 mov ax,20
 call text_ds
 pop ds
 pop si
 pop bx
 mov al,[cs:si-1]                 ; line height = size + 6
 xor ah,ah
 add bx,ax
 add bx,6
.skip:
 cmp byte [cs:si],0
 je .nx
 inc si
 jmp .skip
.nx:
 inc si
 jmp .line
.done:
 ret

; text pages: size byte, text, NUL ... 0   (GEOS charset: 0x9F u, 0x8A a, 0x85 O, 0x9A o umlaut)
t_info:
 db 18,'PDF Betrachter',0
 db 12,'Version 0.12d f',0x9f,'r GeoWorks Ensemble 1.2',0
 db 10,' ',0
 db 11,'Zeigt PDF-Dateien (PDF 1.0 bis 1.7) mit Text, Linien,',0
 db 11,'Fl',0x8a,'chen, Kurven und Bildern (auch JPEG und Transparenz).',0
 db 11,'Mit 64K-/TrueColor-Treibern in Echtfarben, sonst 16 Farben.',0
 db 10,' ',0
 db 11,'Geschrieben mit dem pcgeos-1-sdk-v2 (NASM und Open Watcom).',0
 db 11,'Testdateien aus dem FreeGEOS-PDF-Betrachter von Jirka Kunze.',0
 db 0
t_help:
 db 16,'Hilfe',0
 db 11,'Datei / ',0x85,'ffnen: Ordner und PDF-Dateien zeigen (zuerst DOCUMENT).',0
 db 11,'In der Liste: Klick oder Eingabe ',0x9a,'ffnet, 8 / 2 w',0x8a,'hlt.',0
 db 11,'[NAME] ist ein Ordner, [..] oder R',0x9f,'cktaste f',0x9f,'hrt eine Ebene h',0x9a,'her.',0
 db 10,' ',0
 db 11,'Kn',0x9a,'pfe oben: Erste, Zur',0x9f,'ck, Weiter, Letzte, - und + (Zoom).',0
 db 11,'Scroll-Leisten: Pfeile schieben, Klick in die Leiste springt.',0
 db 10,' ',0
 db 11,'Ziffernblock: 9 / 3 Bild auf / ab, 8 2 4 6 schieben,',0
 db 11,'7 / 1 erste / letzte Seite, + / - Zoom.',0
 db 11,'Leertaste, n / p und j / k gehen ebenfalls.',0
 db 11,'e: Echtfarben ein / aus (wird beim Start aus GEOS.INI erkannt).',0
 db 0

clamp_scroll:
 mov ax,[V_PW]
 sub ax,[V_VW]
 jge .x1
 xor ax,ax
.x1:
 cmp [V_SX],ax
 jle .x2
 mov [V_SX],ax
.x2:
 cmp word [V_SX],0
 jge .x3
 mov word [V_SX],0
.x3:
 mov ax,[V_PH]
 mov bx,[V_VH]
 sub bx,VIEW_TOP
 sub ax,bx
 jge .y1
 xor ax,ax
.y1:
 cmp [V_SY],ax
 jle .y2
 mov [V_SY],ax
.y2:
 cmp word [V_SY],0
 jge .y3
 mov word [V_SY],0
.y3:
 ret

; ======================================================================
; document handling
; ======================================================================
startup:
 mov word [V_INIT],1
 mov word [V_ZOOM],256
 mov byte [V_TRUNC],0
 mov word [V_MODE],0
 ; list block first: it also holds the status / error text
 mov ax,L_SIZE
 mov cx,0xc000
 KCALL 0
 jc .nomem
 mov [V_LHAN],bx
 mov [V_LSEG],ax
 ; work block: locked, zeroed, kept for the life of the application
 mov ax,WORK_SIZE
 mov cx,0xc000                    ; CL heap flags 0 (movable), CH zero+lock
 KCALL 0                          ; MemAlloc -> BX handle, AX segment
 jc .nomem
 mov [V_WHAN],bx
 mov [V_WSEG],ax
 mov ax,DL_SIZE
 mov cx,0xc000
 KCALL 0
 jc .nomem
 mov [V_DHAN],bx
 mov [V_DSEG],ax
 mov ax,XREF_SIZE
 mov cx,0xc000
 KCALL 0
 jc .nomem
 mov es,[V_LSEG]
 mov [es:L_XHAN],bx
 mov [es:L_XSEG],ax
 call detect_tc
 ; started by a click on a PDF file?
 call open_launch
 jc .list
 ret
.list:
 ; list the PDF files in DOCUMENT
 call enum_docs
 jc .test
 mov word [V_MODE],1              ; show the file list
 ret
.test:
 ; fallback: DOCUMENT\TEST.PDF, several ways, error codes kept for diagnosis
 call open_test_file
 jc .nofile
 call open_doc
 ret
.nomem:
 mov ax,-7
 call set_error
 ret
.nofile:
 mov ax,-1
 call set_error
 mov es,[V_LSEG]
 mov di,L_MSG
 mov si,s_nofile
 call copy_cs
 mov si,V_TRY
 mov cx,TRIES*2
.hx:
 mov al,[si]
 call put_hex
 mov al,' '
 stosb
 inc si
 loop .hx
 mov byte [es:di],0
 ret

; open the document GeoManager passed (copy at V_ALB). CF = none / failed
open_launch:
 mov ax,[V_ALB]
 or ax,ax
 jnz .have
 stc
 ret
.have:
 push ds
 mov ds,ax
 cmp byte [S_NAME],0
 je .nodoc
 mov es,[V_LSEG]
 ; 1. folder \ name
 mov si,S_PATH
 mov di,L_FULL
 call cat_str
 cmp di,L_FULL
 je .a1
 cmp byte [es:di-1],'\'
 je .a1
 mov al,'\'
 stosb
.a1:
 mov si,S_NAME
 call cat_str
 mov byte [es:di],0
 call try_open
 jnc .ok
 ; 2. the name alone (relative to the current folder)
 mov si,S_NAME
 mov di,L_FULL
 call cat_str
 mov byte [es:di],0
 call try_open
 jnc .ok
 ; 3. folder field as the whole path
 mov si,S_PATH
 mov di,L_FULL
 call cat_str
 mov byte [es:di],0
 call try_open
 jnc .ok
 ; failed: show what was received
 mov di,L_DIAG
 mov si,s_lfail
 push ds
 push cs
 pop ds
.m:
 lodsb
 stosb
 or al,al
 jnz .m
 pop ds
 dec di
 mov si,S_PATH
 call cat_str
 mov al,'|'
 stosb
 mov si,S_NAME
 call cat_str
 mov byte [es:di],0
 mov byte [es:L_SHOWD],1
.nodoc:
 pop ds
 stc
 ret
.ok:
 ; AX = file handle; the folder becomes the list folder
 push ax
 mov si,S_PATH
 mov di,L_DIR
 call cat_str
 mov byte [es:di],0
 mov ax,[S_DISK]
 pop cx
 pop ds
 cmp di,L_DIR                     ; folder known?
 je .nod
 mov [V_DISK],ax
 mov byte [es:L_DIRSET],1
.nod:
 mov ax,cx
 mov word [V_MODE],0
 call open_doc
 clc
 ret

; copy NUL terminated DS:SI to ES:DI (without the NUL), at most 100 bytes
cat_str:
 mov cx,100
.c:
 lodsb
 or al,al
 jz .d
 stosb
 loop .c
.d:
 ret

; FileOpen ES:L_FULL read-only -> CF, AX handle (DS kept)
try_open:
 push ds
 push es
 pop ds
 mov dx,L_FULL
 mov al,0x40                      ; FILE_DENY_NONE | FILE_ACCESS_R
 KCALL 52                         ; FileOpen
 pop ds
 ret

s_lfail: db 'Start-Datei nicht ge',0x9a,'ffnet: ',0

; AX = open file handle: read the PDF, fit the width, render page 1
open_doc:
 mov [V_FILE],ax
 mov bx,ax
 xor cx,cx
 xor dx,dx
 mov al,2                         ; from end
 KCALL 60                         ; FilePos -> DX:AX size
 mov [V_FSLO],ax
 mov [V_FSHI],dx
 mov word [V_PAGE],0
 mov word [V_SX],0
 mov word [V_SY],0
 mov word [V_ZOOM],256
 mov word [V_ERR],0
 mov byte [V_TRUNC],0
 ; pdf_open(file, fsize, readfn, work, worksize, xref, xrefsize)
 mov es,[V_LSEG]
 mov ax,XREF_SIZE
 push ax
 push word [es:L_XSEG]
 xor ax,ax
 push ax
 mov ax,WORK_SIZE
 push ax
 push word [V_WSEG]
 xor ax,ax
 push ax
 push cs
 mov ax,read_cb
 push ax
 push word [V_FSHI]
 push word [V_FSLO]
 push word [V_FILE]
 LC 1,0
 add sp,22
 or ax,ax
 jz .opened
 mov word [V_NPAGES],0
 call set_error
 ret
.opened:
 push word [V_WSEG]
 xor ax,ax
 push ax
 LC 1,1                           ; pdf_page_count
 add sp,4
 mov [V_NPAGES],ax
 call set_print_range
 ; fit page width into the window
 call page_points
 mov ax,[V_VW]
 sub ax,16
 jle .render
 mov cx,[V_PTW]
 or cx,cx
 jz .render
 xor dx,dx
 mov dl,ah
 mov ah,al
 mov al,0
 div cx
 cmp ax,64
 jb .render
 cmp ax,1024
 jbe .zok
 mov ax,1024
.zok:
 mov [V_ZOOM],ax
.render:
 call render_page
 ret

; AL -> two hex digits at ES:DI
put_hex:
 push ax
 mov ah,al
 shr al,1
 shr al,1
 shr al,1
 shr al,1
 call .dig
 mov al,ah
 and al,15
 call .dig
 pop ax
 ret
.dig:
 add al,'0'
 cmp al,'9'
 jbe .s
 add al,7
.s:
 stosb
 ret

; Try the ways to reach DOCUMENT\TEST.PDF. CF clear: AX = file handle.
; For each attempt V_TRY gets the path error and the open error (0 = ok).
open_test_file:
 xor si,si
.try:
 cmp si,TRIES*4
 jae .fail
 push si
 mov bx,[cs:try_tab+si]
 mov dx,[cs:try_tab+si+2]
 mov ax,[cs:try_file+si]
 mov [V_TMP],ax
 mov al,0
 cmp bx,0xffff
 je .noset
 push ds
 push cs
 pop ds
 KCALL 51                         ; FileSetCurrentPath BX, DS:DX
 pop ds
 jc .seterr
 mov al,0
 jmp .noset
.seterr:
 or al,al
 jnz .noset
 mov al,0xee
.noset:
 pop si
 push si
 mov bx,si
 shr bx,1
 mov [V_TRY+bx],al                ; path result
 push ds
 mov dx,[V_TMP]
 push cs
 pop ds
 mov al,0x40                      ; FILE_DENY_NONE | FILE_ACCESS_R
 KCALL 52                         ; FileOpen
 pop ds
 pop si
 jnc .ok
 mov bx,si
 shr bx,1
 mov [V_TRY+bx+1],al              ; open error code
 add si,4
 jmp .try
.ok:
 clc
 ret
.fail:
 stc
 ret

; per attempt: path standard/disk (0xffff = no FileSetCurrentPath), path, file
try_tab:
 dw 0xffff, s_empty
 dw 0xffff, s_empty
 dw 0xffff, s_empty
 dw 0xffff, s_empty
 dw 0xffff, s_empty
 dw 5, s_empty
try_file:                         ; 4 bytes per entry, like try_tab
 dw s_file, 0
 dw s_f2, 0
 dw s_f3, 0
 dw s_f4, 0
 dw s_f5, 0
 dw s_file, 0
s_f2: db 'DOCUMENT\TEST.PDF',0
s_f3: db '..\DOCUMENT\TEST.PDF',0
s_f4: db '\GEOWORKS\DOCUMENT\TEST.PDF',0
s_f5: db 'C:\GEOWORKS\DOCUMENT\TEST.PDF',0

; AX = error code -> V_ERR and text in V_MSG
set_error:
 mov [V_ERR],ax
 cmp word [V_LSEG],0
 je .r
 mov cx,MSG_SIZE
 push cx
 push word [V_LSEG]
 mov cx,L_MSG
 push cx
 push ax
 LC 1,4                           ; pdf_errtext
 add sp,8
.r:
 ret

; V_PTW/V_PTH = size of the current page in points
page_points:
 push ds
 mov ax,V_PTH
 push ax
 push ds
 mov ax,V_PTW
 push ax
 push word [V_PAGE]
 push word [V_WSEG]
 xor ax,ax
 push ax
 LC 1,2                           ; pdf_page_size
 add sp,14
 ret

render_page:
 cmp word [V_FILE],0
 je .r
 call page_points
 ; device size = points * zoom / 256
 mov ax,[V_PTW]
 mul word [V_ZOOM]
 mov al,ah
 mov ah,dl
 mov [V_PW],ax
 mov ax,[V_PTH]
 mul word [V_ZOOM]
 mov al,ah
 mov ah,dl
 mov [V_PH],ax
 ; external image blocks: lock, pdf_set_ext(work, ptrs, n, size)
 call ext_lock
 mov ax,DL_SIZE-DL_HDR-DL_TMPSZ-DL_CTSZ
 push ax
 push cx
 push word [V_LSEG]
 mov ax,L_EXTP
 push ax
 push word [V_WSEG]
 xor ax,ax
 push ax
 LC 1,5
 add sp,12
 ; pdf_render(work, page, zoom, dl, dlsize, &trunc)
 push ds
 mov ax,V_TRUNC
 push ax
 mov ax,DL_SIZE-DL_HDR-DL_TMPSZ-DL_CTSZ
 push ax
 push word [V_DSEG]
 mov ax,DL_HDR
 push ax
 mov ax,[V_ZOOM]
 push es
 mov es,[V_DSEG]
 cmp byte [es:H_TC],0
 je .ntc
 cmp byte [es:H_PRN],0
 jne .ntc
 or ah,0x80                       ; PDF_TRUECOLOR
.ntc:
 pop es
 push ax
 push word [V_PAGE]
 push word [V_WSEG]
 xor ax,ax
 push ax
 LC 1,3
 add sp,18
 push ax
 call ext_unlock
 pop ax
 cmp ax,0xffec                    ; -20..-1 = error, else DL length (up to 64K)
 jb .ok
 call set_error
 ret
.ok:
 mov word [V_ERR],0
.r:
 ret

; External image blocks for the next page: free the old ones, allocate up
; to NEXT locked blocks (fewer if memory is short) -> CX = count, far
; pointers at L_EXTP, handles/segments in the display list header.
ext_lock:
 push es
 push si
 push di
 call ext_free
 xor si,si
 xor cx,cx
.l:
 push cx
 mov ax,DL_SIZE
 mov cx,0xc000                    ; zero-initialised, locked
 KCALL 0                          ; MemAlloc -> BX handle, AX segment
 pop cx
 jc .d
 mov es,[V_DSEG]
 mov [es:H_EXTH+si],bx
 mov [es:H_EXTS+si],ax
 mov es,[V_LSEG]
 mov di,si
 shl di,1
 mov word [es:L_EXTP+di],DL_HDR
 mov [es:L_EXTP+di+2],ax
 inc cx
 add si,2
 cmp si,2*NEXT
 jb .l
.d:
 pop di
 pop si
 pop es
 ret

; after rendering: free the blocks the page does not use (first record 0)
ext_unlock:
 push es
 push si
 xor si,si
.l:
 mov es,[V_DSEG]
 mov bx,[es:H_EXTH+si]
 or bx,bx
 jz .n
 push es
 mov es,[es:H_EXTS+si]
 cmp byte [es:DL_HDR],0
 pop es
 jne .n
 mov word [es:H_EXTH+si],0
 mov word [es:H_EXTS+si],0
 KCALL 2                          ; MemFree
.n:
 add si,2
 cmp si,2*NEXT
 jb .l
 pop si
 pop es
 ret

ext_free:
 push es
 push si
 xor si,si
.l:
 mov es,[V_DSEG]
 mov bx,[es:H_EXTH+si]
 or bx,bx
 jz .n
 mov word [es:H_EXTH+si],0
 mov word [es:H_EXTS+si],0
 KCALL 2                          ; MemFree
.n:
 add si,2
 cmp si,2*NEXT
 jb .l
 pop si
 pop es
 ret

shutdown:
 cmp word [V_DSEG],0
 je .noext
 call ext_free
.noext:
 mov bx,[V_FILE]
 or bx,bx
 jz .nf
 mov al,0
 KCALL 54                         ; FileClose
 mov word [V_FILE],0
.nf:
 mov bx,[V_DHAN]
 or bx,bx
 jz .nd
 KCALL 2                          ; MemFree
 mov word [V_DHAN],0
.nd:
 mov bx,[V_WHAN]
 or bx,bx
 jz .nw
 KCALL 2
 mov word [V_WHAN],0
.nw:
 mov ax,[V_LHAN]
 or ax,ax
 jz .nx
 mov es,[V_LSEG]
 mov bx,[es:L_XHAN]
 or bx,bx
 jz .nx
 mov word [es:L_XHAN],0
 KCALL 2
.nx:
 mov word [V_INIT],0
 ret

; ----------------------------------------------------------------------
; True colour driver? GEOS.INI [screen 0] device line names 64K, 32K,
; TrueColor, ... -> H_TC = 1. The INI is read into the (empty) DL block.
; ----------------------------------------------------------------------
TC_BUF equ 30000
detect_tc:
 KCALL 73                         ; FilePushDir
 mov ax,0                         ; SP_TOP: the GEOWORKS directory
 KCALL 76                         ; FileSetStandardPath
 mov al,1
 jc .fail
 push ds
 push cs
 pop ds
 mov dx,s_ini
 mov al,0x40                      ; deny none, read
 KCALL 52                         ; FileOpen
 pop ds
 jnc .opened
 mov al,2
 jmp .fail
.opened:
 mov bx,ax                        ; file handle
 push bx
 push ds
 mov ds,[V_DSEG]
 mov byte [H_TCD],3
 mov dx,DL_HDR
 mov cx,TC_BUF
 mov al,0
 KCALL 58                         ; FileRead
 jc .short
 mov cx,TC_BUF
.short:
 mov si,DL_HDR
 add si,cx
 mov byte [si],0
 pop ds
 pop bx
 mov al,0
 KCALL 54                         ; FileClose
 push es
 mov es,[V_DSEG]
 mov di,DL_HDR
.findsec:                         ; line starting with [screen 0]
 cmp byte [es:di],0
 je .no
 cmp byte [es:di],'['             ; ";[screen 0]" is a comment
 jne .nosec
 mov si,k_screen
 call line_has
 jnc .insec
.nosec:
 call next_line
 jmp .findsec
.insec:
 mov byte [es:H_TCD],4
 call next_line
 cmp byte [es:di],0
 je .no
 cmp byte [es:di],'['             ; next section: no device line
 je .no
 cmp byte [es:di],';'             ; comment line
 je .insec
 mov si,k_device
 call line_has
 jc .insec
 mov byte [es:H_TCD],5
 mov si,k_list
.key:
 cmp byte [cs:si],0
 je .no
 call line_has
 jnc .yes
.skip:                            ; next key in the list
 mov al,[cs:si]
 inc si
 or al,al
 jnz .skip
 jmp .key
.yes:
 mov byte [es:H_TC],1
 mov byte [es:H_TCD],9
.no:
 mov byte [es:DL_HDR],0
 pop es
.pop:
 KCALL 74                         ; FilePopDir
 ret
.fail:
 push es
 mov es,[V_DSEG]
 mov [es:H_TCD],al
 pop es
 jmp .pop

; does the line at ES:DI contain CS:SI (upper case)? CF clear = yes
line_has:
 push di
.outer:
 mov al,[es:di]
 or al,al
 jz .not
 cmp al,13
 je .not
 cmp al,10
 je .not
 push di
 push si
.inner:
 mov ah,[cs:si]
 or ah,ah
 jz .found
 mov al,[es:di]
 cmp al,'a'
 jb .cmp
 cmp al,'z'
 ja .cmp
 sub al,32
.cmp:
 cmp al,ah
 jne .miss
 inc si
 inc di
 jmp .inner
.miss:
 pop si
 pop di
 inc di
 jmp .outer
.found:
 pop si
 pop di
 pop di
 clc
 ret
.not:
 pop di
 stc
 ret

next_line:                        ; ES:DI to the start of the next line
 mov al,[es:di]
 or al,al
 jz .e
 inc di
 cmp al,10
 jne next_line
.e:
 ret

s_ini:    db 'GEOS.INI',0
k_screen: db '[SCREEN 0]',0
k_device: db 'DEVICE',0
k_list:   db '64K',0,'32K',0,'65536',0,'32768',0,'TRUECOLOR',0,'16M',0,'-BIT RGB',0,0

; ----------------------------------------------------------------------
; pi16 far cdecl read_cb(pu16 file, pi32 off, pu8 far *buf, pu16 len)
; ----------------------------------------------------------------------
read_cb:
 push bp
 mov bp,sp
 push si
 push di
 push ds
 mov bx,[bp+6]                    ; file
 mov dx,[bp+8]                    ; offset low
 mov cx,[bp+10]                   ; offset high
 mov al,0                         ; from start
 KCALL 60                         ; FilePos
 mov bx,[bp+6]
 mov cx,[bp+16]                   ; len
 lds dx,[bp+12]                   ; buffer
 mov al,0
 KCALL 58                         ; FileRead
 jnc .full
 mov ax,cx                        ; short read: CX = bytes read
 jmp .out
.full:
 mov ax,[bp+16]
.out:
 pop ds
 pop di
 pop si
 pop bp
 retf

s_empty:  db 0
s_file:   db 'TEST.PDF',0
s_nofile: db 'fehlt:',0
G1_FINISH

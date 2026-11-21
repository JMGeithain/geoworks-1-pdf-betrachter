/* SPDX-License-Identifier: MIT
 * 32-bit integer helpers that Open Watcom's 16-bit code calls implicitly
 * (no C runtime is linked). Register contracts as used by wcc -ms:
 *   __U4M/__I4M: DX:AX * CX:BX -> DX:AX
 *   __U4D/__I4D: DX:AX / CX:BX -> quotient DX:AX, remainder CX:BX
 * All other registers are preserved. Near routines in segment ALGORITHM. */
#ifdef __WATCOMC__
#pragma aux __U4M "*"
#pragma aux __I4M "*"
#pragma aux __U4D "*"
#pragma aux __I4D "*"

void __declspec(naked) __U4M(void)
{
    _asm {
        push si
        push di
        mov  si, ax
        mov  di, dx
        mul  cx
        xchg ax, di
        mul  bx
        add  di, ax
        mov  ax, si
        mul  bx
        add  dx, di
        pop  di
        pop  si
        ret
    }
}

void __declspec(naked) __I4M(void)
{
    _asm { jmp __U4M }
}

void __declspec(naked) __U4D(void)
{
    _asm {
        test cx, cx
        jnz  long_div
        push si
        mov  si, ax
        mov  ax, dx
        xor  dx, dx
        div  bx
        xchg ax, si
        div  bx
        mov  bx, dx
        xor  cx, cx
        mov  dx, si
        pop  si
        ret
    long_div:
        push si
        push di
        push bp
        xor  si, si
        xor  di, di
        mov  bp, 32
    next_bit:
        shl  ax, 1
        rcl  dx, 1
        rcl  di, 1
        rcl  si, 1
        cmp  si, cx
        jb   no_sub
        ja   do_sub
        cmp  di, bx
        jb   no_sub
    do_sub:
        sub  di, bx
        sbb  si, cx
        inc  ax
    no_sub:
        dec  bp
        jnz  next_bit
        mov  bx, di
        mov  cx, si
        pop  bp
        pop  di
        pop  si
        ret
    }
}

void __declspec(naked) __I4D(void)
{
    _asm {
        push di
        xor  di, di
        test dx, dx
        jns  dividend_pos
        neg  dx
        neg  ax
        sbb  dx, 0
        xor  di, 3
    dividend_pos:
        test cx, cx
        jns  divisor_pos
        neg  cx
        neg  bx
        sbb  cx, 0
        xor  di, 1
    divisor_pos:
        call __U4D
        test di, 1
        jz   quot_done
        neg  dx
        neg  ax
        sbb  dx, 0
    quot_done:
        test di, 2
        jz   rem_done
        neg  cx
        neg  bx
        sbb  cx, 0
    rem_done:
        pop  di
        ret
    }
}
#endif

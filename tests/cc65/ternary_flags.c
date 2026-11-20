/*
 * cc65 2.19 branches on flags that a call's return did not set.
 *
 *   cl65 -t apple2enh -Or -Cl -S -o ternary_flags.s ternary_flags.c
 *
 * In the output, mark() calls one() or two() and goes straight to a beq
 * on the result, with no tax or cmp. two() takes an argument, so it
 * returns through "jmp incsp1", which pops the C stack with inc sp: the
 * Z flag then describes the stack pointer, not the value in A, and the
 * branch nearly always goes as if two() said nonzero. Twoegram b048: the
 * agent list drew an unread diamond beside every agent but the open one
 * (src/convo.c, row_begin). tools/cc65check.py reports the call.
 */
static unsigned char cur, flag, counts[8];

static unsigned char one(void)
{
    return flag;
}

static unsigned char two(unsigned char a)
{
    return a < 8 ? counts[a] : 0;
}

unsigned char mark(unsigned char a)
{
    if (a == cur ? one() : two(a)) {
        return 1;
    }
    return 0;
}

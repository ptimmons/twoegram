/*
 * cc65 2.18 returns only the first two bytes of a 4-byte struct.
 *
 *   cl65 -t apple2enh -Or -Cl -S -o struct_return.s struct_return.c
 *
 * In the output, part() stores both fields into its static copy, then
 * loads A/X from the first two bytes and jumps out without touching sreg;
 * use() reads sreg as x.n. tools/cc65check.py reports the function.
 */
typedef struct {
    const unsigned char *p;
    unsigned int n;
} part_t;

static part_t part(const void *p, unsigned int n)
{
    part_t x;

    x.p = (const unsigned char *)p;
    x.n = n;
    return x;
}

unsigned int use(void)
{
    part_t a;

    a = part("abc", 3);
    return a.n;
}

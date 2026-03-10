#include <stdio.h>
#include <string.h>
#include <time.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include "runtime.h"

extern int engine_aorb_star_abb(const char *s, int n);
extern int engine_anbn(const char *s, int n);
extern void sno_arena_reset(void);
extern void bison_anbn_init(const char *, int);
extern int  bison_anbn_result;
extern int  anbn_yyparse(void);

static inline long long ns_now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000000000LL + t.tv_nsec;
}

static pcre2_code * compile_re(const char * pat) {
    int err; PCRE2_SIZE off;
    pcre2_code * re = pcre2_compile((PCRE2_SPTR)pat, PCRE2_ZERO_TERMINATED, 0, &err, &off, NULL);
    pcre2_jit_compile(re, PCRE2_JIT_COMPLETE);
    return re;
}
static int pcre2_full(pcre2_code * re, pcre2_match_data * md, const char * s, int n) {
    int rc = pcre2_match(re,(PCRE2_SPTR)s,n,0,0,md,NULL);
    if (rc < 0) return 0;
    PCRE2_SIZE * ov = pcre2_get_ovector_pointer(md);
    return ov[0]==0 && (int)ov[1]==n;
}

#define ITERS 2000000
int main(void) {
    printf("=================================================================\n");
    printf("  Round 2b — Arena Allocator: Zero malloc overhead per match\n");
    printf("=================================================================\n\n");

    const char * re_in[] = {"abb","aabb","babb","ababb","baabb","aaabb","bbabb","abababb","aababb","bbaabb"};
    int re_len[10]; for(int i=0;i<10;i++) re_len[i]=strlen(re_in[i]);
    const char * ab_in[] = {"ab","aabb","aaabbb","aaaabbbb","aaaaabbbbb","ab","aabb","aaabbb","aaaabbbb","aaaaabbbbb"};
    int ab_len[10]; for(int i=0;i<10;i++) ab_len[i]=strlen(ab_in[i]);

    volatile int sink=0; long long t0,t1;

    /* tiny arena: (a|b)*abb */
    for(int i=0;i<5000;i++){sno_arena_reset();sink+=engine_aorb_star_abb(re_in[i%10],re_len[i%10]);}
    t0=ns_now();
    for(int i=0;i<ITERS;i++){sno_arena_reset();sink+=engine_aorb_star_abb(re_in[i%10],re_len[i%10]);}
    t1=ns_now();
    double t_tiny_re=(double)(t1-t0)/ITERS;

    /* pcre2 */
    pcre2_code * re1=compile_re("^(a|b)*abb$");
    pcre2_match_data * md1=pcre2_match_data_create_from_pattern(re1,NULL);
    for(int i=0;i<5000;i++) sink+=pcre2_full(re1,md1,re_in[i%10],re_len[i%10]);
    t0=ns_now();
    for(int i=0;i<ITERS;i++) sink+=pcre2_full(re1,md1,re_in[i%10],re_len[i%10]);
    t1=ns_now();
    double t_pcre2=(double)(t1-t0)/ITERS;

    printf("--- (a|b)*abb ---\n");
    printf("  SNOBOL4-tiny (arena)  : %7.2f ns/match\n", t_tiny_re);
    printf("  PCRE2 JIT             : %7.2f ns/match\n", t_pcre2);
    if (t_tiny_re < t_pcre2)
        printf("  Result: SNOBOL4-tiny is %.2fx FASTER\n\n", t_pcre2/t_tiny_re);
    else
        printf("  Result: PCRE2 JIT is %.2fx faster\n\n", t_tiny_re/t_pcre2);

    /* tiny arena: {a^n b^n} */
    for(int i=0;i<5000;i++){sno_arena_reset();sink+=engine_anbn(ab_in[i%10],ab_len[i%10]);}
    t0=ns_now();
    for(int i=0;i<ITERS;i++){sno_arena_reset();sink+=engine_anbn(ab_in[i%10],ab_len[i%10]);}
    t1=ns_now();
    double t_tiny_ab=(double)(t1-t0)/ITERS;

    /* bison */
    for(int i=0;i<1000;i++){bison_anbn_init(ab_in[i%10],ab_len[i%10]);anbn_yyparse();}
    t0=ns_now();
    for(int i=0;i<ITERS;i++){bison_anbn_init(ab_in[i%10],ab_len[i%10]);anbn_yyparse();sink+=bison_anbn_result;}
    t1=ns_now();
    double t_bison=(double)(t1-t0)/ITERS;

    printf("--- {a^n b^n} ---\n");
    printf("  SNOBOL4-tiny (arena)  : %7.2f ns/parse\n", t_tiny_ab);
    printf("  Bison LALR(1)         : %7.2f ns/parse\n", t_bison);
    if (t_tiny_ab < t_bison)
        printf("  Result: SNOBOL4-tiny is %.2fx FASTER\n\n", t_bison/t_tiny_ab);
    else
        printf("  Result: Bison is %.2fx faster\n\n", t_tiny_ab/t_bison);

    printf("=================================================================\n");
    (void)sink;
    pcre2_match_data_free(md1); pcre2_code_free(re1);
    return 0;
}

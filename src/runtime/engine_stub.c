/* engine_stub.c — linker stub for compiled-path binaries.
 *
 * The compiled Byrd box path (sno2c output) never calls match_pattern()
 * or match_and_replace(), so engine_match_ex() is never reached at
 * runtime.  This stub satisfies the linker without pulling in engine.c.
 *
 * EVAL and dynamic patterns still require the real engine.c.  This stub
 * is only for the compiled path where EVAL is not used.
 */
#include "engine.h"

MatchResult engine_match_ex(Pattern *root, const char *subject, int subject_len,
                             const EngineOpts *opts)
{
    /* Never called in the compiled Byrd box path. */
    MatchResult r = {0, 0, 0};
    return r;
}

MatchResult engine_match(Pattern *root, const char *subject, int subject_len)
{
    return engine_match_ex(root, subject, subject_len, NULL);
}

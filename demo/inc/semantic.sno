*---------------------------------------------------------------------------------------------------
* Semantic building routines. These functions are called while building the parser
* patterns, not during pattern matching.
* Note: two-arg DEFINE used so body labels don't clash with function names (SPITBOL compat)
*---------------------------------------------------------------------------------------------------
               DEFINE('shift(p,t)',  'shift_')
               DEFINE('reduce(t,n)', 'reduce_')
               OPSYN('~', 'shift', 2)
               OPSYN('&', 'reduce', 2)
               DEFINE('pop()',       'pop_')
               DEFINE('nPush()',     'nPush_')
               DEFINE('nInc()',      'nInc_')
               DEFINE('nDec()',      'nDec_')
               DEFINE('nTop()',      'nTop_')
               DEFINE('nPop()',      'nPop_')                                    :(semanticEnd)
*---------------------------------------------------------------------------------------------------
shift_         shift          =  EVAL("p . thx . *Shift('" t "', thx)")          :(RETURN)
reduce_        reduce         =  EVAL("epsilon . *Reduce(" t ", " n ")")         :(RETURN)
pop_           pop            =  epsilon . *Pop(.dummy)                          :(RETURN)
*---------------------------------------------------------------------------------------------------
nPush_         nPush          =  epsilon . *PushCounter()                        :(RETURN)
nInc_          nInc           =  epsilon . *IncCounter()                         :(RETURN)
nDec_          nDec           =  epsilon . *DecCounter()                         :(RETURN)
nTop_          nTop           =  TopCounter()                                    :(RETURN)
nPop_          nPop           =  epsilon . *PopCounter()                         :(RETURN)
*---------------------------------------------------------------------------------------------------
semanticEnd

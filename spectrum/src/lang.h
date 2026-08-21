#ifndef LANG_H
#define LANG_H

/* build.sh picks the language with -DCFG_LANG_EN. Only one is compiled in:
 * two sets of strings plus two prompts do not fit in what is left of 48K. */
#ifdef CFG_LANG_EN
#include "lang/en.h"
#else
#include "lang/es.h"
#endif

#endif

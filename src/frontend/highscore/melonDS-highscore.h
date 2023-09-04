#pragma once

#include <highscore/libhighscore.h>

G_BEGIN_DECLS

#define MELONDS_TYPE_CORE (melonds_core_get_type())

G_DECLARE_FINAL_TYPE (melonDSCore, melonds_core, MELONDS, CORE, HsCore)

void melonds_core_log (HsLogLevel level, const char *message);

const char *melonds_core_get_save_path (void);

G_MODULE_EXPORT GType hs_get_core_type (void);

G_END_DECLS

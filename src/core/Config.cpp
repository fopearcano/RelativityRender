#include "core/Config.h"

// Config is currently a plain data struct populated by CommandLine::parse.
// This translation unit exists so future load/save logic (TOML / JSON) has
// a home without forcing a header-only design.

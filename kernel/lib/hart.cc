#include "lib/hart.h"

namespace hart {
size_t primary;
// All false by BSS
bool startable[MAX];
bool started[MAX];
} // namespace hart

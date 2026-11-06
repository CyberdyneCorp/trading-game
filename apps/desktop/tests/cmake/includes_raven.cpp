// Layer probe (tests/CMakeLists.txt): compiles only in a target that links Raven.
#include <raven/raven_core.h>

int tradelab_layer_probe() { return Ris_initialized(); }

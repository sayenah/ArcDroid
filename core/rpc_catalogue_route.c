/* Replace only this provider in non-installed authored targets. GPL-2.0-only. */
#include "rpc_selection.h"
const ArcRpcFirmwareFingerprint *arc_riscpc_firmware_catalogue(size_t *count)
{return arc_riscpc_production_catalogue(count);}

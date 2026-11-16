/*
 * km_shared_loader.h - pulls the driver's IOCTL contract into user mode.
 *
 * km_shared.h switches to <ntdef.h> when _KERNEL_MODE is defined; in the
 * loader it is compiled as user mode, so it pulls <windows.h> instead. This
 * wrapper only exists to give the loader a stable include name.
 */
#pragma once

#include "../driver/km_shared.h"

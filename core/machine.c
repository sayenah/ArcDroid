/* ArcDroid machine descriptors. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#include "arcdroid_core.h"

#include <string.h>

#define OLD_OS (ARC_OS_MASK(ARC_OS_TEST) | ARC_OS_MASK(ARC_OS_RISCOS_311) | \
                ARC_OS_MASK(ARC_OS_RISCOS_310) | ARC_OS_MASK(ARC_OS_RISCOS_319) | \
                ARC_OS_MASK(ARC_OS_RISCOS_300) | ARC_OS_MASK(ARC_OS_RISCOS_201) | \
                ARC_OS_MASK(ARC_OS_RISCOS_200) | ARC_OS_MASK(ARC_OS_ARTHUR_120) | \
                ARC_OS_MASK(ARC_OS_ARTHUR_030))
#define NEW_OS (ARC_OS_MASK(ARC_OS_TEST) | ARC_OS_MASK(ARC_OS_RISCOS_311) | \
                ARC_OS_MASK(ARC_OS_RISCOS_310) | ARC_OS_MASK(ARC_OS_RISCOS_319))

static const ArcMachineDescriptor machines[ARC_MACHINE_COUNT] = {
   /* ide_capable means ArcDroid attaches an IDE disc, not hardware presence. */
   {ARC_MACHINE_A3000, "A3000", "a3000", ARC_IO_OLD_WD1770, ARC_CPU_ARM2,
    1, 4096u, OLD_OS, "riscos3_old", 0, false, false},
   {ARC_MACHINE_A3010, "A3010", "a3010", ARC_IO_NEW_82C711, ARC_CPU_ARM250,
    2, 4096u, NEW_OS, "riscos3_new", 0, true, false},
   {ARC_MACHINE_A3020, "A3020", "a3020", ARC_IO_NEW_82C711, ARC_CPU_ARM250,
    2, 4096u, NEW_OS, "riscos3_new", 0, false, true},
   {ARC_MACHINE_A4000, "A4000", "a4000", ARC_IO_NEW_82C711, ARC_CPU_ARM250,
    2, 4096u, NEW_OS, "riscos3_new", 0, false, false}
};

const ArcMachineDescriptor *arc_machine_descriptor(ArcMachine machine)
{ return machine < ARC_MACHINE_COUNT ? &machines[machine] : NULL; }

const ArcMachineDescriptor *arc_machine_from_name(const char *name)
{
   if (!name || !strcmp(name, "Auto")) return &machines[ARC_MACHINE_A3000];
   for (unsigned i = 0; i < ARC_MACHINE_COUNT; ++i)
      if (!strcmp(name, machines[i].name)) return &machines[i];
   return NULL;
}

const char *arc_cpu_name(ArcCpu cpu)
{ return cpu == ARC_CPU_ARM2 ? "ARM2" : cpu == ARC_CPU_ARM250 ? "ARM250" : "unknown CPU"; }

bool arc_machine_supports_os(ArcMachine machine, ArcOsClass os)
{
   const ArcMachineDescriptor *descriptor = arc_machine_descriptor(machine);
   return descriptor && os > ARC_OS_UNKNOWN && os <= ARC_OS_ARTHUR_030 &&
          (descriptor->compatible_os & ARC_OS_MASK(os)) != 0u;
}

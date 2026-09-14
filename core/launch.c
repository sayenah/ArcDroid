/* ArcDroid launch planning. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#include "arcdroid_core.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool arc_extension(const char *path, const char *expected)
{
   const char *dot = path ? strrchr(path, '.') : NULL;
   if (!dot) return false;
   ++dot;
   while (*dot && *expected)
   {
      if (tolower((unsigned char)*dot++) != tolower((unsigned char)*expected++)) return false;
   }
   return !*dot && !*expected;
}

ArcMediaKind arc_media_inspect(const char *path, size_t size,
                               char *error, size_t error_size)
{
   ArcMediaKind kind = ARC_MEDIA_INVALID;
   if (arc_extension(path, "adf"))
   {
      if (size == 327680u) kind = ARC_MEDIA_ADF_320K;
      else if (size == 819200u) kind = ARC_MEDIA_ADF_800K;
      else if (size == 1638400u) kind = ARC_MEDIA_ADF_HD_UNSUPPORTED;
   }
   else if (arc_extension(path, "adl") && size == 655360u)
      kind = ARC_MEDIA_ADL_640K;
   if (kind == ARC_MEDIA_ADF_HD_UNSUPPORTED)
   {
      if (error && error_size) snprintf(error, error_size, "1600 KiB high-density ADF is unsupported by the Slice 1 WD1772 machine");
   }
   else if (kind == ARC_MEDIA_INVALID && error && error_size)
      snprintf(error, error_size, "unsupported or corrupt ADF/ADL geometry (%zu bytes)", size);
   return kind;
}

bool arc_launch_plan_default(ArcLaunchPlan *plan, ArcMediaKind media,
                             ArcOsClass os, const char content_sha1[41],
                             bool request_a3010_joystick,
                             char *error, size_t error_size)
{
   if (!plan || media == ARC_MEDIA_INVALID || media == ARC_MEDIA_ADF_HD_UNSUPPORTED || os == ARC_OS_UNKNOWN)
   {
      if (error && error_size) snprintf(error, error_size, "launch plan inputs are invalid");
      return false;
   }
   if (request_a3010_joystick)
   {
      if (error && error_size) snprintf(error, error_size, "A3010 joystick interface is incompatible with the A3000 machine");
      return false;
   }
   memset(plan, 0, sizeof(*plan));
   plan->os = os;
   plan->machine = "A3000";
   plan->cpu = "ARM2";
   plan->memory_kib = 4096u;
   plan->media = media;
   if (content_sha1) memcpy(plan->content_id, content_sha1, 41u);
   return true;
}

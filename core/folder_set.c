/* ArcDroid folder disc-set discovery. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#include "folder_set.h"

#include <stdlib.h>
#include <string.h>

static unsigned char arc_ascii_fold(unsigned char value)
{
   return value >= 'A' && value <= 'Z' ? (unsigned char)(value + ('a' - 'A')) : value;
}

static bool arc_name_equal(const char *left, size_t left_size,
                           const char *right, size_t right_size,
                           bool insensitive)
{
   if (left_size != right_size) return false;
   for (size_t i = 0u; i < left_size; ++i)
      if ((insensitive ? arc_ascii_fold((unsigned char)left[i]) : (unsigned char)left[i]) !=
          (insensitive ? arc_ascii_fold((unsigned char)right[i]) : (unsigned char)right[i]))
         return false;
   return true;
}

static bool arc_media_extension(const char *name)
{
   const char *dot = name ? strrchr(name, '.') : NULL;
   if (!dot) return false;
   ++dot;
   static const char *const extensions[] = {"adf", "adl", "jfd"};
   for (unsigned extension = 0u; extension < 3u; ++extension)
   {
      const char *candidate = dot;
      const char *expected = extensions[extension];
      while (*candidate && *expected &&
             arc_ascii_fold((unsigned char)*candidate) == (unsigned char)*expected)
      { ++candidate; ++expected; }
      if (!*candidate && !*expected) return true;
   }
   return false;
}

static int arc_natural_primary(const char *left, const char *right)
{
   size_t l = 0u, r = 0u;
   while (left[l] && right[r])
   {
      if (left[l] >= '0' && left[l] <= '9' &&
          right[r] >= '0' && right[r] <= '9')
      {
         size_t l_end = l, r_end = r;
         while (left[l_end] >= '0' && left[l_end] <= '9') ++l_end;
         while (right[r_end] >= '0' && right[r_end] <= '9') ++r_end;
         size_t l_number = l;
         size_t r_number = r;
         while (l_number < l_end && left[l_number] == '0') ++l_number;
         while (r_number < r_end && right[r_number] == '0') ++r_number;
         size_t l_digits = l_end - l_number;
         size_t r_digits = r_end - r_number;
         if (l_digits != r_digits) return l_digits < r_digits ? -1 : 1;
         if (l_digits)
         {
            int numeric = memcmp(left + l_number, right + r_number, l_digits);
            if (numeric) return numeric < 0 ? -1 : 1;
         }
         l = l_end;
         r = r_end;
         continue;
      }
      unsigned char l_fold = arc_ascii_fold((unsigned char)left[l]);
      unsigned char r_fold = arc_ascii_fold((unsigned char)right[r]);
      if (l_fold != r_fold) return l_fold < r_fold ? -1 : 1;
      ++l;
      ++r;
   }
   if (left[l] || right[r]) return left[l] ? 1 : -1;
   return 0;
}

static int arc_natural_compare(const void *left_pointer, const void *right_pointer)
{
   const char *left = left_pointer;
   const char *right = right_pointer;
   int primary = arc_natural_primary(left, right);
   if (primary) return primary;
   return strcmp(left, right);
}

ArcFolderSetTrigger arc_folder_set_begin(const char *content_path,
                                         bool windows_host,
                                         ArcFolderSet *set)
{
   if (set) memset(set, 0, sizeof(*set));
   if (!content_path || !content_path[0] || strstr(content_path, "://"))
      return ARC_FOLDER_SET_NONE;
   size_t path_size = strlen(content_path);
   if (path_size >= ARC_MEDIA_PATH_CAPACITY) return ARC_FOLDER_SET_NONE;
   const char *last = NULL;
   for (const char *at = content_path; *at; ++at)
      if (*at == '/' || *at == '\\') last = at;
   if (!last || !last[1]) return ARC_FOLDER_SET_NONE;
   size_t directory_size = (size_t)(last - content_path);
   if (!directory_size) return ARC_FOLDER_SET_NONE;
   const char *parent_name = content_path;
   for (const char *at = content_path; at < last; ++at)
      if (*at == '/' || *at == '\\') parent_name = at + 1;
   size_t parent_size = (size_t)(last - parent_name);
   size_t selected_size = path_size - directory_size - 1u;
   if (!parent_size || !arc_name_equal(parent_name, parent_size, last + 1,
                                        selected_size, windows_host))
      return ARC_FOLDER_SET_NONE;
   if (set)
   {
      memcpy(set->directory, content_path, directory_size);
      set->directory[directory_size] = '\0';
      memcpy(set->selected_name, last + 1, selected_size + 1u);
      set->separator = *last;
      set->windows_host = windows_host;
   }
   return directory_size > ARC_FOLDER_SET_MAX_DIRECTORY ?
      ARC_FOLDER_SET_DIRECTORY_TOO_LONG : ARC_FOLDER_SET_READY;
}

ArcFolderMemberResult arc_folder_set_add(ArcFolderSet *set, const char *name,
                                         bool regular_file)
{
   if (!set || !name || !name[0] || !regular_file || name[0] == '.' ||
       !arc_media_extension(name))
      return ARC_FOLDER_MEMBER_IGNORED;
   size_t name_size = strlen(name);
   if (arc_name_equal(name, name_size, set->selected_name,
                      strlen(set->selected_name), set->windows_host))
      return ARC_FOLDER_MEMBER_IGNORED;
   ++set->qualified_count;
   if (set->qualified_count > ARC_FOLDER_SET_MAX_SIBLINGS)
   {
      set->overflow = true;
      return ARC_FOLDER_MEMBER_LIMIT_EXCEEDED;
   }
   size_t directory_size = strlen(set->directory);
   if (name_size >= ARC_MEDIA_PATH_CAPACITY ||
       directory_size + 1u + name_size >= ARC_MEDIA_PATH_CAPACITY)
      return ARC_FOLDER_MEMBER_PATH_TOO_LONG;
   memcpy(set->siblings[set->count++], name, name_size + 1u);
   return ARC_FOLDER_MEMBER_ADDED;
}

void arc_folder_set_sort(ArcFolderSet *set)
{
   if (set && set->count > 1u)
      qsort(set->siblings, set->count, sizeof(set->siblings[0]),
            arc_natural_compare);
}

bool arc_folder_set_path(const ArcFolderSet *set, unsigned index,
                         char path[ARC_MEDIA_PATH_CAPACITY])
{
   if (!set || !path || index >= set->count) return false;
   size_t directory_size = strlen(set->directory);
   size_t name_size = strlen(set->siblings[index]);
   if (directory_size + 1u + name_size >= ARC_MEDIA_PATH_CAPACITY) return false;
   memcpy(path, set->directory, directory_size);
   path[directory_size] = set->separator;
   memcpy(path + directory_size + 1u, set->siblings[index], name_size + 1u);
   return true;
}

/* ArcDroid RetroPad mapping and verified Archimedes key catalogue.
 * Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#include "arcdroid_core.h"
#include "libretro.h"

#include <string.h>

#define K(code, value, label, row, column, modifier) \
   {code, value, label, row, column, modifier}

/* Every entry below is backed by keys[] in Arculator keytable.h. */
static const ArcInputKey arc_keys[] = {
   K(RETROK_a,"a","A",3,12,false), K(RETROK_b,"b","B",5,2,false),
   K(RETROK_c,"c","C",5,0,false), K(RETROK_d,"d","D",3,14,false),
   K(RETROK_e,"e","E",2,9,false), K(RETROK_f,"f","F",3,15,false),
   K(RETROK_g,"g","G",4,0,false), K(RETROK_h,"h","H",4,1,false),
   K(RETROK_i,"i","I",2,14,false), K(RETROK_j,"j","J",4,2,false),
   K(RETROK_k,"k","K",4,3,false), K(RETROK_l,"l","L",4,4,false),
   K(RETROK_m,"m","M",5,4,false), K(RETROK_n,"n","N",5,3,false),
   K(RETROK_o,"o","O",2,15,false), K(RETROK_p,"p","P",3,0,false),
   K(RETROK_q,"q","Q",2,7,false), K(RETROK_r,"r","R",2,10,false),
   K(RETROK_s,"s","S",3,13,false), K(RETROK_t,"t","T",2,11,false),
   K(RETROK_u,"u","U",2,13,false), K(RETROK_v,"v","V",5,1,false),
   K(RETROK_w,"w","W",2,8,false), K(RETROK_x,"x","X",4,15,false),
   K(RETROK_y,"y","Y",2,12,false), K(RETROK_z,"z","Z",4,14,false),
   K(RETROK_0,"0","0",1,10,false), K(RETROK_1,"1","1",1,1,false),
   K(RETROK_2,"2","2",1,2,false), K(RETROK_3,"3","3",1,3,false),
   K(RETROK_4,"4","4",1,4,false), K(RETROK_5,"5","5",1,5,false),
   K(RETROK_6,"6","6",1,6,false), K(RETROK_7,"7","7",1,7,false),
   K(RETROK_8,"8","8",1,8,false), K(RETROK_9,"9","9",1,9,false),
   K(RETROK_SPACE,"space","Space",5,15,false),
   K(RETROK_RETURN,"return","Return",4,7,false),
   K(RETROK_ESCAPE,"escape","Escape",0,0,false),
   K(RETROK_TAB,"tab","Tab",2,6,false),
   K(RETROK_BACKSPACE,"backspace","Backspace",1,14,false),
   K(RETROK_DELETE,"delete","Delete",3,4,false),
   K(RETROK_END,"copy","Copy",3,5,false),
   K(RETROK_INSERT,"insert","Insert",1,15,false),
   K(RETROK_HOME,"home","Home",2,0,false),
   K(RETROK_PAGEUP,"page_up","Page Up",2,1,false),
   K(RETROK_PAGEDOWN,"page_down","Page Down",3,6,false),
   K(RETROK_UP,"up","Cursor Up",5,9,false),
   K(RETROK_DOWN,"down","Cursor Down",6,3,false),
   K(RETROK_LEFT,"left","Cursor Left",6,2,false),
   K(RETROK_RIGHT,"right","Cursor Right",6,4,false),
   K(RETROK_LSHIFT,"left_shift","Left Shift",4,12,true),
   K(RETROK_RSHIFT,"right_shift","Right Shift",5,8,true),
   K(RETROK_LCTRL,"left_ctrl","Left Ctrl",3,11,true),
   K(RETROK_RCTRL,"right_ctrl","Right Ctrl",6,1,true),
   K(RETROK_LALT,"left_alt","Left Alt",5,14,true),
   K(RETROK_RALT,"right_alt","Right Alt",6,0,true),
   K(RETROK_CAPSLOCK,"caps_lock","Caps Lock",5,13,false),
   K(RETROK_F1,"f1","F1",0,1,false), K(RETROK_F2,"f2","F2",0,2,false),
   K(RETROK_F3,"f3","F3",0,3,false), K(RETROK_F4,"f4","F4",0,4,false),
   K(RETROK_F5,"f5","F5",0,5,false), K(RETROK_F6,"f6","F6",0,6,false),
   K(RETROK_F7,"f7","F7",0,7,false), K(RETROK_F8,"f8","F8",0,8,false),
   K(RETROK_F9,"f9","F9",0,9,false), K(RETROK_F10,"f10","F10",0,10,false),
   K(RETROK_F11,"f11","F11",0,11,false), K(RETROK_F12,"f12","F12",0,12,false),
   K(RETROK_BACKQUOTE,"backquote","`",1,0,false),
   K(RETROK_MINUS,"minus","-",1,11,false),
   K(RETROK_EQUALS,"equals","=",1,12,false),
   K(RETROK_LEFTBRACKET,"left_bracket","[",3,1,false),
   K(RETROK_RIGHTBRACKET,"right_bracket","]",3,2,false),
   K(RETROK_SEMICOLON,"semicolon",";",4,5,false),
   K(RETROK_QUOTE,"quote","'",4,6,false),
   K(RETROK_BACKSLASH,"backslash","Backslash",3,3,false),
   K(RETROK_COMMA,"comma",",",5,5,false),
   K(RETROK_PERIOD,"period",".",5,6,false),
   K(RETROK_SLASH,"slash","/",5,7,false),
   K(RETROK_KP0,"kp0","Keypad 0",6,5,false),
   K(RETROK_KP1,"kp1","Keypad 1",5,10,false),
   K(RETROK_KP2,"kp2","Keypad 2",5,11,false),
   K(RETROK_KP3,"kp3","Keypad 3",5,12,false),
   K(RETROK_KP4,"kp4","Keypad 4",4,8,false),
   K(RETROK_KP5,"kp5","Keypad 5",4,9,false),
   K(RETROK_KP6,"kp6","Keypad 6",4,10,false),
   K(RETROK_KP7,"kp7","Keypad 7",3,7,false),
   K(RETROK_KP8,"kp8","Keypad 8",3,8,false),
   K(RETROK_KP9,"kp9","Keypad 9",3,9,false),
   K(RETROK_KP_DIVIDE,"kp_divide","Keypad /",2,3,false),
   K(RETROK_KP_MULTIPLY,"kp_multiply","Keypad *",2,4,false),
   K(RETROK_KP_MINUS,"kp_minus","Keypad -",2,5,false),
   K(RETROK_KP_PLUS,"kp_plus","Keypad +",4,11,false),
   K(RETROK_KP_PERIOD,"kp_period","Keypad .",6,6,false),
   K(RETROK_KP_ENTER,"kp_enter","Keypad Enter",6,7,false)
};

/* Verified A3000 keys that belong on the keyboard but not in the fixed
 * per-button option list. */
static const ArcInputKey arc_osk_keys[] = {
   K(RETROK_NUMLOCK,"num_lock","Num Lock",2,2,false),
   K(RETROK_SCROLLOCK,"scroll_lock","Scroll Lock",0,14,false),
   K(RETROK_PAUSE,"pause","Pause",0,15,false)
};

static const ArcInputBinding arc_generic[ARC_PAD_INPUT_COUNT] = {
   {true, RETROK_UP}, {true, RETROK_DOWN}, {true, RETROK_LEFT},
   {true, RETROK_RIGHT}, {true, RETROK_SPACE}, {true, RETROK_RETURN},
   {true, RETROK_LSHIFT}, {true, RETROK_LCTRL}, {true, RETROK_ESCAPE},
   {true, RETROK_TAB}
};

const ArcInputBinding *arc_input_generic_mapping(void) { return arc_generic; }

const ArcInputKey *arc_input_supported_keys(size_t *count)
{
   if (count) *count = sizeof(arc_keys) / sizeof(arc_keys[0]);
   return arc_keys;
}

const ArcInputKey *arc_input_osk_only_keys(size_t *count)
{
   if (count) *count = sizeof(arc_osk_keys) / sizeof(arc_osk_keys[0]);
   return arc_osk_keys;
}

const ArcInputKey *arc_input_key(unsigned key)
{
   for (size_t i = 0; i < sizeof(arc_keys) / sizeof(arc_keys[0]); ++i)
      if (arc_keys[i].key == key) return &arc_keys[i];
   return NULL;
}

const ArcInputKey *arc_input_key_value(const char *value)
{
   if (!value) return NULL;
   for (size_t i = 0; i < sizeof(arc_keys) / sizeof(arc_keys[0]); ++i)
      if (!strcmp(arc_keys[i].value, value)) return &arc_keys[i];
   return NULL;
}

bool arc_input_is_modifier(unsigned key)
{
   return key == RETROK_LSHIFT || key == RETROK_RSHIFT ||
          key == RETROK_LCTRL || key == RETROK_RCTRL ||
          key == RETROK_LALT || key == RETROK_RALT;
}

const char *arc_pad_input_name(unsigned input)
{
   static const char *names[ARC_PAD_INPUT_COUNT] = {
      "Up", "Down", "Left", "Right", "A", "B", "X", "Y", "Start", "Select"
   };
   return input < ARC_PAD_INPUT_COUNT ? names[input] : "Unknown";
}

static const ArcInputProfile *arc_find_profile(const char *id,
                                                const char content_id[41],
                                                const char *launch_target,
                                                bool by_id,
                                                const ArcInputProfile *profiles,
                                                size_t count)
{
   for (size_t i = 0u; i < count; ++i)
   {
      const ArcInputProfile *profile = &profiles[i];
      if (by_id)
      {
         if (id && !strcmp(id, profile->id)) return profile;
         continue;
      }
      if (profile->launch_target && profile->launch_target[0] &&
          (!launch_target || strcmp(profile->launch_target, launch_target)))
         continue;
      for (unsigned digest = 0u; digest < profile->digest_count; ++digest)
         if (content_id && !strcmp(content_id, profile->digests[digest]))
            return profile;
   }
   return NULL;
}

bool arc_input_resolve_table(const ArcInputProfile *profiles,
                             size_t profile_count,
                             const char content_id[41],
                             const char *launch_target,
                             const char *profile_selection,
                             const ArcInputOverride overrides[ARC_PAD_PORTS][ARC_PAD_INPUT_COUNT],
                             ArcInputResolution *resolution)
{
   if (!resolution || !profile_selection || (profile_count && !profiles)) return false;
   const ArcInputProfile *profile = NULL;
   bool preset = false;
   if (!strcmp(profile_selection, "Auto"))
      profile = arc_find_profile(NULL, content_id, launch_target, false,
                                 profiles, profile_count);
   else if (strcmp(profile_selection, "Generic"))
   {
      preset = true;
      profile = arc_find_profile(profile_selection, NULL, NULL, true,
                                 profiles, profile_count);
      if (!profile) return false;
   }
   memset(resolution, 0, sizeof(*resolution));
   resolution->profile_id = profile ? profile->id : "generic";
   resolution->profile_name = profile ? profile->name : "Generic";
   for (unsigned port = 0u; port < ARC_PAD_PORTS; ++port)
      for (unsigned input = 0u; input < ARC_PAD_INPUT_COUNT; ++input)
      {
         ArcInputBinding binding = arc_generic[input];
         ArcMappingSource source = ARC_MAPPING_SOURCE_GENERIC;
         if (profile && (!port || profile->has_port1))
         {
            binding = profile->mapping[port][input];
            source = preset ? ARC_MAPPING_SOURCE_PRESET : ARC_MAPPING_SOURCE_PROFILE;
         }
         if (overrides && !overrides[port][input].automatic)
         {
            binding = overrides[port][input].binding;
            source = ARC_MAPPING_SOURCE_OPTION;
            resolution->custom = true;
         }
         resolution->binding[port][input] = binding;
         resolution->source[port][input] = source;
      }
   return true;
}

bool arc_input_resolve(const char content_id[41], const char *launch_target,
                       const char *profile_selection,
                       const ArcInputOverride overrides[ARC_PAD_PORTS][ARC_PAD_INPUT_COUNT],
                       ArcInputResolution *resolution)
{
   size_t count = 0u;
   const ArcInputProfile *profiles = arc_input_profiles(&count);
   return arc_input_resolve_table(profiles, count, content_id, launch_target,
                                  profile_selection, overrides, resolution);
}

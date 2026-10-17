/* Temporary policy-boundary test double. Normal frontend calls delegate to
 * the actual engine; only this explicit audit supplies callback results.
 * GPL-2.0-only. */
#undef NDEBUG
#include <assert.h>
#include "../engine/rpc/rpc_floppy.h"
#undef arc_engine_is_rpc
#undef arc_engine_rpc_launch_input
#undef arc_engine_rpc_launch_media_changed
#undef arc_engine_rpc_launch_interrupt
#undef arc_engine_launch_outcome_cancel
#undef arc_engine_boot_watch_cancel
#undef arc_engine_media_eject
#undef arc_engine_media_insert
#undef arc_engine_stop
#undef arc_engine_disc_controller_idle
#undef arc_engine_rpc_observers_replace
#undef arc_test_plan_pending
extern bool arc_engine_is_rpc(void),arc_engine_media_eject(void),arc_engine_disc_controller_idle(void),arc_test_plan_pending(void);
extern bool arc_engine_media_insert(const ArcEngineMedia *,char *,size_t),arc_engine_rpc_observers_replace(ArcDiscSignal);
extern void arc_engine_rpc_launch_input(void),arc_engine_rpc_launch_media_changed(void),arc_engine_boot_watch_cancel(void),arc_engine_stop(void);
extern void arc_engine_rpc_launch_interrupt(ArcRpcAttemptInterrupt),arc_engine_launch_outcome_cancel(const char *);
static bool audit_active,audit_rpc,audit_pending,audit_insert_fail,audit_insert_stub;
static unsigned audit_input,audit_media,audit_interrupt,audit_stop,audit_launch_cancel,audit_boot_cancel;
bool arc_predicate_is_rpc(void){return audit_active?audit_rpc:arc_engine_is_rpc();}
bool arc_predicate_pending(void){return audit_active?audit_pending:arc_test_plan_pending();}
bool arc_predicate_idle(void){return audit_active || arc_engine_disc_controller_idle();}
bool arc_predicate_replace(ArcDiscSignal s){return audit_active || arc_engine_rpc_observers_replace(s);}
bool arc_predicate_eject(void){return audit_active || arc_engine_media_eject();}
bool arc_predicate_insert(const ArcEngineMedia *m,char *e,size_t n)
{if(audit_active && audit_insert_fail)return false;if(audit_active && audit_insert_stub)return true;return arc_engine_media_insert(m,e,n);}
void arc_predicate_input(void){++audit_input;if(!audit_active)arc_engine_rpc_launch_input();}
void arc_predicate_media(void){++audit_media;if(!audit_active)arc_engine_rpc_launch_media_changed();}
void arc_predicate_interrupt(ArcRpcAttemptInterrupt r){++audit_interrupt;if(!audit_active)arc_engine_rpc_launch_interrupt(r);}
void arc_predicate_stop(void){++audit_stop;if(!audit_active)arc_engine_stop();}
void arc_predicate_launch_cancel(const char *r){++audit_launch_cancel;if(!audit_active)arc_engine_launch_outcome_cancel(r);}
void arc_predicate_boot_cancel(void){++audit_boot_cancel;if(!audit_active)arc_engine_boot_watch_cancel();}
static void audit_counters(void)
{audit_input=audit_media=audit_interrupt=audit_stop=audit_launch_cancel=audit_boot_cancel=0;}
static int16_t audit_no_input(unsigned p,unsigned d,unsigned i,unsigned id)
{(void)p;(void)d;(void)i;(void)id;return 0;}
static void audit_no_poll(void){}
static void audit_restage(void);
RETRO_API void arc_predicate_frontend_audit(void)
{
 if(!arc_loaded || !arc_engine_is_rpc() || !arc_media_count)return;
 bool owned=arc_disc_owned,ejected=arc_ejected,attempt=arc_boot_attempt,adffs=arc_adffs_active;
 unsigned index=arc_image_index,count=arc_media_count,read=arc_disc_notice_read,write=arc_disc_notice_write;
 uint64_t generation=arc_disc_generation;ArcDiscMode mode=arc_disc_mode;
 ArcSessionKind kind=arc_kind;
 ArcInputState input=arc_inputs;bool keys[ARC_INPUT_KEY_COUNT];memcpy(keys,arc_keyboard_raw,sizeof(keys));
 retro_input_state_t input_state=arc_input_state;retro_input_poll_t input_poll=arc_input_poll;
 audit_active=audit_rpc=audit_pending=true;arc_disc_owned=true;arc_disc_notice_read=1;arc_disc_notice_write=2;
 audit_counters();assert(!arc_disk_set_eject(true));assert(!arc_disk_set_index(0));
 assert(!arc_disk_replace(0,NULL));assert(!arc_disk_add());
 assert(arc_disc_owned && arc_disc_notice_read==1 && arc_disc_notice_write==2 && arc_disc_generation==generation);
 assert(!audit_input && !audit_media && !audit_launch_cancel && !audit_boot_cancel);
 audit_pending=false;arc_adffs_active=false;
 for(unsigned rpc=0;rpc<2;++rpc){audit_rpc=rpc==0;arc_kind=rpc?ARC_SESSION_KIND_RISCPC:ARC_SESSION_KIND_ARCULATOR;
  for(unsigned down=0;down<2;++down)for(unsigned armed=0;armed<2;++armed){
   arc_boot_attempt=armed!=0;audit_counters();arc_keyboard(down!=0,RETROK_SPACE,0,0);
   assert(audit_input==rpc && audit_launch_cancel==!rpc && audit_boot_cancel==(!rpc && down && armed));
  }
  arc_input_state=audit_no_input;arc_input_poll=audit_no_poll;
  for(unsigned pressed=0;pressed<2;++pressed)for(unsigned armed=0;armed<2;++armed){
   arc_input_reset(&arc_inputs);memset(arc_keyboard_raw,0,sizeof(arc_keyboard_raw));
   arc_keyboard_raw[RETROK_SPACE]=pressed!=0;arc_boot_attempt=armed!=0;
   audit_counters();arc_poll_inputs();
   assert(audit_input==(rpc && pressed) && audit_launch_cancel==(!rpc && pressed));
   assert(audit_boot_cancel==(!rpc && pressed && armed));
  }
  audit_counters();arc_ejected=false;assert(arc_disk_set_eject(true));
  assert(audit_media==rpc && audit_launch_cancel==!rpc && audit_boot_cancel==!rpc);
  audit_counters();arc_ejected=true;assert(arc_disk_set_index(arc_media_count));arc_image_index=index;
  assert(audit_media==rpc && audit_launch_cancel==!rpc && audit_boot_cancel==!rpc);
  struct retro_game_info missing={.path="/missing/audit.adl"};audit_counters();assert(!arc_disk_replace(0,&missing));
  assert(audit_media==rpc && audit_launch_cancel==!rpc && audit_boot_cancel==!rpc);
  audit_counters();assert(arc_media_count<ARC_PLAYLIST_MAX_ENTRIES && arc_disk_add());
  assert(audit_media==rpc && audit_launch_cancel==!rpc && audit_boot_cancel==!rpc);arc_media_count=count;
 }
 audit_rpc=true;arc_kind=ARC_SESSION_KIND_RISCPC;
 if(count>=2 && arc_media[0].info.kind!=ARC_MEDIA_JFD && arc_media[1].info.kind!=ARC_MEDIA_JFD){
  arc_disc_mode=ARC_DISC_MODE_AUTO;arc_image_index=0;arc_ejected=false;audit_insert_fail=true;audit_counters();
  assert(arc_disc_commit(1,arc_disc_generation,ARC_DISC_SIGNAL_A)==ARC_DISC_COMMIT_FAILED);
  assert(audit_interrupt==1 && audit_stop==1);
  arc_ejected=false;audit_insert_fail=false;audit_counters();
  assert(arc_disc_commit(1,arc_disc_generation,ARC_DISC_SIGNAL_A)==ARC_DISC_COMMIT_INSERTED);
  assert(audit_media==1 && !audit_launch_cancel && !audit_boot_cancel);
 }
 audit_restage();
 arc_kind=kind;audit_active=false;arc_disc_owned=owned;arc_ejected=ejected;arc_image_index=index;arc_disc_mode=mode;
 arc_boot_attempt=attempt;arc_adffs_active=adffs;arc_disc_notice_read=read;arc_disc_notice_write=write;
 arc_input_state=input_state;arc_input_poll=input_poll;arc_inputs=input;memcpy(arc_keyboard_raw,keys,sizeof(keys));
}

static ArcCoreOptions audit_options;
static bool audit_options_fail,audit_container_fail,audit_prepare_fail,audit_refresh_fail;
static unsigned audit_option_calls,audit_zip_calls,audit_folder_calls,audit_prepare_mask,audit_tail_mask,audit_refresh_calls;
static bool arc_predicate_options(ArcCoreOptions *o,char *e,size_t n)
{if(!audit_active)return arc_read_options(o,e,n);++audit_option_calls;*o=audit_options;return !audit_options_fail;}
static bool arc_predicate_zip(ArcDiskWriteMode m,unsigned *s,char *e,size_t n)
{if(!audit_active)return arc_reprepare_zip_media(m,s,e,n);assert(arc_zip_active&&!arc_folder_set_active);++audit_zip_calls;return !audit_container_fail;}
static bool arc_predicate_folder(ArcDiskWriteMode m,unsigned *s,char *e,size_t n)
{if(!audit_active)return arc_reprepare_folder_media(m,s,e,n);assert(!arc_zip_active&&arc_folder_set_active);++audit_folder_calls;return !audit_container_fail;}
static bool arc_predicate_prepare(const char *s,const char *label,ArcDiskWriteMode m,bool notify,bool *reported,ArcMediaEntry *out,char *e,size_t n)
{
 if(!audit_active)return arc_prepare_source(s,label,m,notify,reported,out,e,n);
 assert(!arc_zip_active&&!arc_folder_set_active);
 for(unsigned i=0;i<arc_media_count;++i)if(!strcmp(s,arc_media[i].source)){
  assert(arc_media[i].populated);audit_prepare_mask|=1u<<i;if(audit_prepare_fail)return false;*out=arc_media[i];return true;
 }assert(0);return false;
}
static void arc_predicate_tail(ArcMediaEntry *entry,bool zero)
{if(audit_active){assert(entry>=arc_media&&entry<arc_media+3);audit_tail_mask|=1u<<(unsigned)(entry-arc_media);}arc_entry_tail_presentation(entry,zero);}
static bool arc_predicate_refresh(ArcMediaEntry *entry,ArcDiskWriteMode m,ArcJfdWritePolicy policy,char *e,size_t n)
{if(!audit_active)return arc_refresh_jfd(entry,m,policy,e,n);++audit_refresh_calls;return !audit_refresh_fail;}
static void audit_restage(void)
{
 ArcMediaEntry *saved=malloc(3*sizeof(*saved));assert(saved);memcpy(saved,arc_media,3*sizeof(*saved));
 ArcCoreOptions options=arc_core_options;ArcDiskWriteMode writes=arc_plan.disk_writes;
 unsigned count=arc_media_count,index=arc_image_index;bool loaded=arc_loaded,ejected=arc_ejected,zip=arc_zip_active,folder=arc_folder_set_active;
 bool preparing=arc_preparing_zero_tail,notice=arc_jfd_tab_pending_notice_shown;
 audit_insert_stub=true;
 for(unsigned kind=0;kind<3;++kind)for(unsigned changed=0;changed<2;++changed)
 for(unsigned was_ejected=0;was_ejected<2;++was_ejected)for(unsigned selected=2;selected<=3;++selected){
  for(unsigned fail=0;fail<6;++fail){
   if(fail==2 && (!changed || !kind))continue;
   if(fail==3 && (!changed || kind))continue;
   if((fail==4||fail==5) && (was_ejected||selected==3))continue;
   memset(arc_media,0,3*sizeof(*arc_media));arc_media_count=3;arc_image_index=selected;arc_loaded=true;arc_ejected=was_ejected!=0;
   for(unsigned i=0;i<3;++i){ArcMediaEntry *entry=&arc_media[i];snprintf(entry->source,sizeof(entry->source),"authored-%u.adl",i);
    entry->populated=i!=1;entry->info.kind=ARC_MEDIA_ADL_640K;entry->info.geometry=ARC_GEOMETRY_640K_L;
    entry->info.available_size=4096;entry->info.short_image=true;entry->effective_extent=1234;
   }
   arc_zip_active=kind==1;arc_folder_set_active=kind==2;arc_plan.disk_writes=ARC_DISK_WRITES_OFF;
   audit_options=options;audit_options.disk_writes=changed?ARC_DISK_WRITES_SHADOW:ARC_DISK_WRITES_OFF;audit_options.short_tail_zero=true;
   audit_options_fail=fail==1;audit_container_fail=fail==2;audit_prepare_fail=fail==3;audit_refresh_fail=fail==4;audit_insert_fail=fail==5;
   audit_option_calls=audit_zip_calls=audit_folder_calls=audit_prepare_mask=audit_tail_mask=audit_refresh_calls=0;
   uint64_t drive1=arc_rpc_fdc_media_generation(1),drive0=arc_rpc_fdc_media_generation(0);char error[128]={0};
   assert(arc_rpc_plan_restage_drive0(error,sizeof(error))==(fail==0));
   assert(arc_rpc_fdc_media_generation(1)==drive1 && audit_option_calls==1);
   if(fail==1){assert(arc_rpc_fdc_media_generation(0)==drive0);continue;}
   assert(audit_zip_calls==(changed&&kind==1) && audit_folder_calls==(changed&&kind==2));
   if(fail==2||fail==3)continue;
   assert(audit_prepare_mask==((changed&&!kind)?5u:0u) && audit_tail_mask==5u);
   assert(audit_refresh_calls==(!was_ejected&&selected<3));
   if(fail==4||fail==5)continue;
   assert(arc_ejected==(was_ejected||selected==3) && arc_image_index==selected);
   assert(arc_plan.disk_writes==audit_options.disk_writes && !arc_jfd_tab_pending_notice_shown);
   assert(arc_media[0].zero_tail_presentation && arc_media[2].zero_tail_presentation);
   assert(!arc_media[0].effective_extent && !arc_media[2].effective_extent);
  }
 }
 audit_options_fail=audit_container_fail=audit_prepare_fail=audit_refresh_fail=audit_insert_fail=false;
 audit_option_calls=0;arc_loaded=false;char error[128];assert(arc_rpc_plan_restage_drive0(error,sizeof(error)) && !audit_option_calls);
 ArcMediaEntry helper=arc_rpc_helper_entry;arc_rpc_helper_entry=arc_media[0];arc_rpc_helper_entry.populated=true;
 ArcEngineMedia view;assert(!arc_rpc_plan_drive1_view(NULL));assert(arc_rpc_plan_drive1_view(&view));
 arc_rpc_helper_entry.populated=false;assert(!arc_rpc_plan_drive1_view(&view));arc_rpc_helper_entry=helper;
 for(unsigned mode=0;mode<3;++mode){arc_plan.disk_writes=(ArcDiskWriteMode)mode;assert(arc_rpc_plan_drive1_writable()==(mode!=(unsigned)ARC_DISK_WRITES_OFF));}
 arc_loaded=true;arc_plan.disk_writes=ARC_DISK_WRITES_OFF;arc_media_count=1;arc_image_index=0;arc_ejected=false;
 ArcMediaEntry *entry=&arc_media[0];entry->populated=true;memset(entry->digest.sha1,'1',40);entry->digest.sha1[40]=0;
 uint8_t first[32],second[32];entry->effective_extent=4096;
 assert(arc_rpc_plan_media_identity(first));entry->effective_extent=8192;
 assert(arc_rpc_plan_media_identity(second) && memcmp(first,second,32));
 entry->info.kind=ARC_MEDIA_JFD;
 ArcJfdOverlay *overlay=calloc(1,sizeof(*overlay));assert(overlay);entry->jfd.overlay=overlay;
 overlay->visible=true;assert(!arc_rpc_plan_media_identity(second));overlay->visible=false;
 overlay->damaged=true;assert(!arc_rpc_plan_media_identity(second));overlay->damaged=false;
 overlay->failed=true;assert(!arc_rpc_plan_media_identity(second));entry->jfd.overlay=NULL;free(overlay);
 memcpy(arc_media,saved,3*sizeof(*saved));free(saved);arc_core_options=options;arc_plan.disk_writes=writes;
 arc_media_count=count;arc_image_index=index;arc_loaded=loaded;arc_ejected=ejected;arc_zip_active=zip;arc_folder_set_active=folder;
 arc_preparing_zero_tail=preparing;arc_jfd_tab_pending_notice_shown=notice;audit_insert_stub=false;
}

/* Public-entrypoint scripts for R6 option policies. GPL-2.0-only. */
static unsigned option_counter(const char *name)
{
    if(!strcmp(name,"conflict"))return option_conflict;
    if(!strcmp(name,"unknown"))return option_unknown;
    if(!strcmp(name,"scan"))return inventory_opens;
    if(!strcmp(name,"private"))return private_lines;
    if(!strcmp(name,"arc"))return arculator_starts;
    if(!strcmp(name,"drift"))return drift;
    if(!strcmp(name,"running"))return running;
    if(!strcmp(name,"failed"))return failed;
    if(!strcmp(name,"writes"))return writes;
    if(!strcmp(name,"arc-writes"))return arculator_writes;
    if(!strncmp(name,"reload",6) && name[6]>='0' && name[6]<='4')return option_reload[name[6]-'0'];
    if(!strncmp(name,"seen",4) && name[4]>='0' && name[4]<='6')return option_seen[name[4]-'0'];
    fprintf(stderr,"Unknown option counter %s\n",name);abort();
}
static void options_script(const char *path,bool (*load)(const struct retro_game_info *),
    void (*reset)(void),void (*run)(void),void (*unload)(void))
{
    FILE *script=fopen(path,"rb");assert(script);
    void (*probe)(unsigned *,unsigned * )=NULL;
    if(symbol("arc_options_probe"))LOAD(probe,"arc_options_probe");
    char line[8192];unsigned line_number=0;
    while(fgets(line,sizeof(line),script)){
        ++line_number;line[strcspn(line,"\r\n")]=0;
        if(!*line || *line=='#')continue;
        printf("options-command:%u:%s\n",line_number,line);fflush(stdout);
        char *arg=strchr(line,' ');if(arg)*arg++=0;
        if(!strcmp(line,"set")){
            assert(arg);char *value=strchr(arg,'=');assert(value);*value++=0;
            unsigned i=0;for(;i<option_count;++i)if(!strcmp(option_values[i][0],arg))break;
            assert(i<64 && strlen(arg)<512 && strlen(value)<512);
            strcpy(option_values[i][0],arg);strcpy(option_values[i][1],value);
            if(i==option_count)++option_count;options_changed=true;
        }else if(!strcmp(line,"unset")){
            assert(arg);for(unsigned i=0;i<option_count;++i)if(!strcmp(option_values[i][0],arg)){
                --option_count;memcpy(option_values[i],option_values[option_count],sizeof(option_values[i]));break;
            }options_changed=true;
        }else if(!strcmp(line,"load") || !strcmp(line,"load-fail")){
            assert(arg);struct retro_game_info game={.path=arg};bool ok=load(&game);
            if(ok!=!strcmp(line,"load")){fprintf(stderr,"load mismatch at script line %u: %s\n",line_number,last_notice);abort();}
        }else if(!strcmp(line,"reset"))reset();
        else if(!strcmp(line,"locator-fault")){change_locator=true;selected_reads=0;}
        else if(!strcmp(line,"legacy"))legacy_interface=true;
        else if(!strcmp(line,"remove")){assert(arg && !remove(arg));}
        else if(!strcmp(line,"unload"))unload();
        else if(!strcmp(line,"run") || !strcmp(line,"trace")){
            unsigned n;assert(arg && sscanf(arg,"%u",&n)==1 && n<100001);
            tracing=!strcmp(line,"trace");trace_hash=UINT64_C(1099511628211);
            for(unsigned i=0;i<n;++i)run();
            if(tracing)printf("options-trace:%016llx\n",(unsigned long long)trace_hash);
            tracing=false;
        }else if(!strcmp(line,"until")){
            unsigned target;assert(probe && arg && sscanf(arg,"%u",&target)==1);
            unsigned state=0,values[64];probe(&state,values);
            for(unsigned i=0;state!=target;++i){
                if(i>=20000 || (state==11 && target!=11)){fprintf(stderr,"unreached option state %u (now %u): %s\n",target,state,last_notice);abort();}
                run();probe(&state,values);
            }
        }else if(!strcmp(line,"probe")){
            unsigned index,expected,state,values[64];assert(probe && arg && sscanf(arg,"%u %u",&index,&expected)==2 && index<64);
            probe(&state,values);if(values[index]!=expected){fprintf(stderr,"probe[%u]=%u expected %u at line %u\n",index,values[index],expected,line_number);abort();}
        }else if(!strcmp(line,"check")){
            char name[64];unsigned expected;assert(arg && sscanf(arg,"%63s %u",name,&expected)==2);
            unsigned actual=option_counter(name);if(actual!=expected){fprintf(stderr,"%s=%u expected %u at line %u\n",name,actual,expected,line_number);abort();}
        }else if(!strcmp(line,"clear")){
            option_count=0;options_changed=true;
        }else if(!strcmp(line,"counts")){
            option_unknown=option_conflict=inventory_opens=firmware_opens=private_lines=arculator_starts=drift=running=0;failed=false;
            memset(option_reload,0,sizeof(option_reload));memset(option_seen,0,sizeof(option_seen));
        }else {fprintf(stderr,"Unknown option command %s\n",line);abort();}
    }
    assert(!ferror(script) && !fclose(script));
    printf("options-complete: scan=%u conflict=%u unknown=%u private=%u arc=%u drift=%u\n",firmware_opens,option_conflict,option_unknown,private_lines,arculator_starts,drift);
}

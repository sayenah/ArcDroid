/* Flag-free injected allocator boundary oracle. GPL-2.0-only. */
#include "arc_session.h"
#include <assert.h>
#include <stddef.h>
int main(void)
{
    ArcSessionIdSource source={0};uint64_t id=0;
    for(uint64_t i=1;i<=10000;++i){assert(arc_session_id_allocate(&source,&id));assert(id==i);}
    source.last=UINT64_MAX-1;assert(arc_session_id_allocate(&source,&id)&&id==UINT64_MAX);
    for(unsigned i=0;i<3;++i)assert(!arc_session_id_allocate(&source,&id)&&id==0&&source.last==UINT64_MAX);
    assert(!arc_session_id_allocate(NULL,&id)&&!id);assert(!arc_session_id_allocate(&source,NULL));
    return 0;
}

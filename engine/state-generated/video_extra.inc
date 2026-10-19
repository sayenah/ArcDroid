/* ArcDroid retained-raster state. GPL-2.0-only. */
static void arc_state_video_extra(ArcState *s)
{
   if(!buffer||buffer->w!=4096||buffer->h!=2048){arc_state_fail(s,"raster binding");return;}
   arc_state_raster(s,"video.bitmap.pixels",(uint32_t*)buffer->dat,4096u,arc_raster_rows);
   arc_state_region(s,"video.bitmap",buffer,sizeof(*buffer)+2048u*sizeof(uint8_t*));
}

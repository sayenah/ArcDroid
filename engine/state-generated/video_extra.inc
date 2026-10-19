/* ArcDroid retained-raster state. GPL-2.0-only. */
static void arc_state_video_extra(ArcState *s)
{
   if(!buffer||buffer->w!=4096||buffer->h!=2048){arc_state_fail(s,"raster binding");return;}
   /* D-215: pixels are rebuilt without changing guest video state. */
   arc_state_region(s,"video.bitmap",buffer,sizeof(*buffer)+2048u*sizeof(uint8_t*));
}

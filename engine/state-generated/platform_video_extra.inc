/* ArcDroid retained presentation pixels. GPL-2.0-only. */
static void arc_state_platform_video_extra(ArcState *s)
{ arc_state_raster(s,"video.platform.pixels",arc_video_frame,4096u,arc_video_rows); }

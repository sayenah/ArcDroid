/* ArcDroid modification (2026-10-16): notice added for existing ArcDroid media-change and read-only controller-idle interfaces. */
extern void wd1770_reset();
extern uint8_t wd1770_read(uint16_t addr);
extern void wd1770_write(uint16_t addr, uint8_t val);
extern void wd1770_writelatch_a(uint8_t val);
extern void wd1770_writelatch_b(uint8_t val);
extern void wd1770_media_change(void);

int wd1770_idle(void);

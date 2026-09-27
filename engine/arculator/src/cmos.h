void cmos_init();
void cmos_load();
void cmos_save();

/* ArcDroid modification (2026-09-14, 2026-09-27): explicit in-memory persistence
 * boundary and session write guard for locked bytes. */
void cmos_set_image(const uint8_t *data, size_t size);
void cmos_get_image(uint8_t *data, size_t size);
typedef void (*cmos_ignored_write_callback)(void *opaque, unsigned raw,
                                             uint8_t attempted, uint8_t retained);
void cmos_set_write_guard(const uint8_t mask[32],
                          cmos_ignored_write_callback callback, void *opaque);

void i2c_change(int new_clock, int new_data);

extern int cmos_changed;
extern int i2c_clock, i2c_data;

#define CMOS_CHANGE_DELAY 10 /*10cs = 100ms*/

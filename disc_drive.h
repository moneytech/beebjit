#ifndef BEEBJIT_DISC_DRIVE_H
#define BEEBJIT_DISC_DRIVE_H

#include <stdint.h>

struct disc_drive_struct;
struct disc_struct;

struct timing_struct;

struct disc_drive_struct* disc_drive_create(struct timing_struct* p_timing);
void disc_drive_destroy(struct disc_drive_struct* p_drive);
void disc_drive_set_byte_callback(struct disc_drive_struct* p_drive,
                                  void (*p_byte_callback)(void* p,
                                                          uint8_t data,
                                                          uint8_t clock),
                                  void* p_byte_callback_object);

void disc_drive_power_on_reset(struct disc_drive_struct* p_drive);

void disc_drive_add_disc(struct disc_drive_struct* p_drive,
                         struct disc_struct* p_disc);
void disc_drive_cycle_disc(struct disc_drive_struct* p_drive);

int disc_drive_is_spinning(struct disc_drive_struct* p_drive);
int disc_drive_is_upper_side(struct disc_drive_struct* p_drive);
uint32_t disc_drive_get_track(struct disc_drive_struct* p_drive);
int disc_drive_is_index_pulse(struct disc_drive_struct* p_drive);
uint32_t disc_drive_get_head_position(struct disc_drive_struct* p_drive);
int disc_drive_is_write_protect(struct disc_drive_struct* p_drive);

void disc_drive_start_spinning(struct disc_drive_struct* p_drive);
void disc_drive_stop_spinning(struct disc_drive_struct* p_drive);
void disc_drive_select_side(struct disc_drive_struct* p_drive,
                            int is_upper_side);
void disc_drive_select_track(struct disc_drive_struct* p_drive, uint32_t track);
void disc_drive_seek_track(struct disc_drive_struct* p_drive, int32_t delta);
void disc_drive_write_byte(struct disc_drive_struct* p_drive,
                           uint8_t data,
                           uint8_t clocks);

#endif /* BEEBJIT_DISC_DRIVE_H */

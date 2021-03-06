#ifndef BEEBJIT_DISC_SSD_H
#define BEEBJIT_DISC_SSD_H

struct disc_struct;

#include <stdint.h>

void disc_ssd_load(struct disc_struct* p_disc, int is_dsd);
void disc_ssd_write_track(struct disc_struct* p_disc,
                          int is_side_upper,
                          uint32_t track,
                          uint8_t* p_data,
                          uint8_t* p_clocks);

#endif /* BEEBJIT_DISC_SSD_H */

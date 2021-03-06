/*
 * mpxgen - FM multiplex encoder with Stereo and RDS
 * Copyright (C) 2019 Anthony96922
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <samplerate.h>

#define CONVERTER_TYPE SRC_SINC_FASTEST

#define INPUT_DATA_SIZE 1024
#define DATA_SIZE INPUT_DATA_SIZE * 32

extern int fm_mpx_open(char *filename, int wait_for_audio);
extern int fm_mpx_get_samples(float *mpx_buffer);
extern void fm_mpx_close();
extern void set_output_volume(int vol);

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

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <samplerate.h>
#include <ao/ao.h>

#include "rds.h"
#include "fm_mpx.h"
#include "control_pipe.h"

#define DATA_SIZE 4096

int stop_mpx;

void stop() {
	stop_mpx = 1;
}

int out_channels = 2;
float volume;

void postprocess(float *inbuf, short *outbuf, size_t inbufsize) {
	int j = 0;

	for (int i = 0; i < inbufsize; i++) {
#if 0
		if (inbuf[i] <= -1 || inbuf[i] >= 1) {
			fprintf(stderr, "overshoot! (%.7f)\n", inbuf[i]);
		}
#endif

		if (out_channels == 2) {
			outbuf[j] = outbuf[j+1] = (inbuf[i] * (volume / 100)) * 32767;
			j += 2;
		} else {
			outbuf[i] = (inbuf[i] * (volume / 100)) * 32767;
		}
	}
}

int generate_mpx(char *audio_file, char *output_file, int rds, uint16_t pi, char *ps, char *rt, int *af_array, float mpx, char *control_pipe, int pty, int tp) {
	// Gracefully stop the encoder on SIGINT or SIGTERM
	int signals[] = {SIGINT, SIGTERM};
	for (int i = 0; i < 2; i++) {
		struct sigaction sa;

		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = stop;
		sigaction(signals[i], &sa, NULL);
	}

	// Data structures for baseband data
	float mpx_data[DATA_SIZE];
	float resample_out[DATA_SIZE];
	short dev_out[DATA_SIZE*2];

	// AO
	ao_device *device;
	ao_sample_format format;
	format.bits = 16;
	format.channels = 2;
	format.rate = 192000;
	format.byte_format = AO_FMT_LITTLE;

	ao_initialize();
	int ao_driver;

	if (output_file != NULL) {
		ao_driver = ao_driver_id("wav");
		out_channels = 1;
		format.channels = 1;
		if ((device = ao_open_file(ao_driver, output_file, 1, &format, NULL)) == NULL) {
			fprintf(stderr, "Error: cannot open output file.\n");
			return 1;
		}
	} else {
		ao_driver = ao_default_driver_id();
		if ((device = ao_open_live(ao_driver, &format, NULL)) == NULL) {
			fprintf(stderr, "Error: cannot open sound device.\n");
			return 1;
		}
	}

	// SRC
	int src_error;

	SRC_STATE *src_state;
	SRC_DATA src_data;
	src_data.src_ratio = 192000. / 228000;
	src_data.input_frames = DATA_SIZE;
	src_data.output_frames = DATA_SIZE;
	src_data.data_out = resample_out;

	if ((src_state = src_new(SRC_SINC_FASTEST, 1, &src_error)) == NULL) {
		fprintf(stderr, "Error: src_new failed: %s\n", src_strerror(src_error));
		return 1;
	}

	// Initialize the baseband generator
	if(fm_mpx_open(audio_file, DATA_SIZE, rds) < 0) return 1;

	// Initialize the RDS modulator
	if(rds) {
		rds_encoder_init(DATA_SIZE, pi, ps, rt, pty, tp);
		// RBDS PTY list
		char *ptys[] = {"None", "News", "Information", "Sports",
				"Talk", "Rock", "Classic rock", "Adult hits",
				"Soft rock" , "Top 40", "Country", "Oldies",
				"Soft music", "Nostalgia", "Jazz", "Classical",
				"R&B", "Soft R&B", "Language", "Religious music",
				"Religious talk", "Personality", "Public", "College",
				"Spanish talk", "Spanish music", "Hip-Hop", "Unassigned",
				"Unassigned", "Weather", "Emergency test", "Emergency"};
		printf("RDS Options:\n");
		printf("PI: %04X, PS: \"%s\", PTY: %d (%s), TP: %d\n", pi, ps, pty, ptys[pty], tp);
		printf("RT: \"%s\"\n", rt);
		if(af_array[0]) {
			set_rds_af(af_array);
			printf("AF: %d, ", af_array[0]);
			for(int f = 1; f < af_array[0]+1; f++) {
				printf("%.1f ", (float)(af_array[f]+875)/10);
			}
			printf("\n");
		}
	}

	// Initialize the control pipe reader
	if(control_pipe) {
		if(open_control_pipe(control_pipe) == 0) {
			printf("Reading control commands on %s.\n", control_pipe);
		} else {
			printf("Failed to open control pipe: %s.\n", control_pipe);
			control_pipe = NULL;
		}
	}

	volume = mpx;

	for (;;) {
		if(control_pipe) poll_control_pipe();

		if (fm_mpx_get_samples(mpx_data) < 0) break;
		src_data.data_in = mpx_data;

		if ((src_error = src_process(src_state, &src_data))) {
			fprintf(stderr, "Error: src_process failed: %s\n", src_strerror(src_error));
			break;
		}

		postprocess(resample_out, dev_out, src_data.output_frames_gen);
		// num_bytes = src_data.output_frames_gen * channels * bytes per sample
		if (!ao_play(device, (char *)dev_out, src_data.output_frames_gen * out_channels * 2)) {
			fprintf(stderr, "Error: could not play audio.\n");
			break;
		}

		if (stop_mpx) {
			printf("Stopping...\n");
			break;
		}
	}

	close_control_pipe();
	fm_mpx_close();

	ao_close(device);
	ao_shutdown();

	src_delete(src_state);

	return 0;
}

int main(int argc, char **argv) {
	int opt;
	char *audio_file = NULL;
	char *output_file = NULL;
	char *control_pipe = NULL;
	int rds = 1;
	int alternative_freq[100] = {0};
	int af_size = 0;
	char *ps = "mpxgen";
	char *rt = "mpxgen: FM Stereo and RDS encoder";
	uint16_t pi = 0xFFFF;
	int pty = 0;
	int tp = 0;
	float mpx = 100;

	const char	*short_opt = "a:o:m:R:i:s:r:p:T:A:C:h";
	struct option	long_opt[] =
	{
		{"audio", 	required_argument, NULL, 'a'},
		{"output-file",	required_argument, NULL, 'o'},
		{"mpx",		required_argument, NULL, 'm'},

		{"rds", 	required_argument, NULL, 'R'},
		{"pi",		required_argument, NULL, 'i'},
		{"ps",		required_argument, NULL, 's'},
		{"rt",		required_argument, NULL, 'r'},
		{"pty",		required_argument, NULL, 'p'},
		{"tp",		required_argument, NULL, 'T'},
		{"af",		required_argument, NULL, 'A'},
		{"ctl",		required_argument, NULL, 'C'},

		{"help",	no_argument, NULL, 'h'},
		{ 0,		0,		0,	0 }
	};

	while((opt = getopt_long(argc, argv, short_opt, long_opt, NULL)) != -1)
	{
		switch(opt)
		{
			case 'a': //audio
				audio_file = optarg;
				break;

			case 'o': //output-file
				output_file = optarg;
				break;

			case 'm': //mpx
				mpx = atoi(optarg);
				break;

			case 'R': //rds
				rds = atoi(optarg);
				break;

			case 'i': //pi
				pi = (uint16_t) strtol(optarg, NULL, 16);
				break;

			case 's': //ps
				ps = optarg;
				break;

			case 'r': //rt
				rt = optarg;
				break;

			case 'p': //pty
				pty = atoi(optarg);
				break;

			case 'T': //tp
				tp = atoi(optarg);
				break;

			case 'A': //af
				af_size++;
				alternative_freq[af_size] = (int)(10*atof(optarg))-875;
				if(alternative_freq[af_size] < 1 || alternative_freq[af_size] > 204) {
					fprintf(stderr, "Alternative Frequency has to be set in range of 87.6 MHz - 107.9 MHz\n");
					return 1;
				}
				break;

			case 'C': //ctl
				control_pipe = optarg;
				break;

			case 'h': //help
				fprintf(stderr, "Help: %s\n"
				      "	[--audio (-a) file] [--output-file (-o) WAVE out] [--mpx (-m) mpx-volume]\n"
				      "	[--rds (-R) rds-switch] [--pi (-i) pi-code] [--ps (-s) ps-text]\n"
				      "	[--rt (-r) radiotext] [--pty (-p) program-type] [--tp (-T) traffic-program]\n"
				      "	[--af (-A) alternative-freq] [--ctl (-C) control-pipe]\n", argv[0]);
				return 1;
				break;

			default:
				fprintf(stderr, "(See -h / --help)\n");
				return 1;
				break;
		}
	}

	if (audio_file == NULL && !rds) {
		fprintf(stderr, "Nothing to do. Exiting.\n");
		return 1;
	}

	if (mpx < 1 || mpx > 100) {
		fprintf(stderr, "MPX volume must be between 1 - 100.\n");
		return 1;
	}

	if (pty < 0 || pty > 31) {
		fprintf(stderr, "PTY must be between 0 - 31.\n");
		return 1;
	}

	alternative_freq[0] = af_size;

	int errcode = generate_mpx(audio_file, output_file, rds, pi, ps, rt, alternative_freq, mpx, control_pipe, pty, tp);

	return errcode;
}

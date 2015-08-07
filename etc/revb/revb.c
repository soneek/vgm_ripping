#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <ctype.h>
#include <math.h>

#include "error_stuff.h"
#include "util.h"

/* Cafe Citra B (edit of Revolution B) - bcstm and bfstm building and extraction tool */
/* by hcs (http://here.is/halleyscomet) */
/* 3DS and Wii U updates by soneek */

/* NOTE: I'm not clear on what should happen if the byte count is an exact
 * multiple of the size of a block. At the moment I assume that the
 * last block size will be set to 0 and the block count will include all
 * complete blocks.
 */

/* NOTE: The ADPC chunk is created but is not filled with anything. In theory
 * I can decode the DSP and fill in these values (assuming that I understand its
 * purpose correctly), but this is not yet done so seeking may be a bit broken.
 */

#define VERSION "0.5"
#define MAX_CHANNELS 12
#define TRACK_COUNT 6

enum output_strm
{
	BRSTM = 1,
	BCSTM = 2,
	BFSTM = 3,
	SSB_IDSP = 4,
	TECMO_G1l = 5,
};

enum odd_options
{
    OPTION_SECOND_CHUNK_EXTRA = 1,
    OPTION_ALTERNATE_ADPC_COUNT = 2,
};

enum revb_mode
{
    MODE_INVALID,
    MODE_BUILD,
    MODE_EXTRACT,
    MODE_EXAMINE,
};

void (*put_16)(uint16_t value, FILE *infile) = put_16_be;
void (*put_32)(uint32_t value, FILE *infile) = put_32_be;
void (*put_16_seek)(uint16_t value, long offset, FILE *infile) = put_16_be_seek;
void (*put_32_seek)(uint32_t value, long offset, FILE *infile) = put_32_be_seek;
	
static void build(const char *brstm_name, const char *dsp_names [],
        int dsp_count, enum odd_options options, enum output_strm strm);

static void build_brstm(const char *brstm_name, const char *dsp_names[],
        int dsp_count, enum odd_options options);
		
static void build_g1l(const char *brstm_name, const char *dsp_names[],
        int dsp_count, enum odd_options options);
		
static uint32_t samples_to_nibbles(uint32_t samples);
static uint32_t nibbles_to_samples(uint32_t nibbles);

static void expect_8(uint8_t expected, long offset, const char *desc,
        FILE *infile);
static void expect_16(uint16_t expected, long offset, const char *desc,
        FILE *infile);
static void expect_32(uint32_t expected, long offset, const char *desc,
        FILE *infile);

static const char *bin_name = NULL;

static const uint8_t RSTM_sig[4] = {'R','S','T','M'};
static const uint8_t CSTM_sig[4] = {'C','S','T','M'};
static const uint8_t FSTM_sig[4] = {'F','S','T','M'};
static const uint8_t *STRM_sig[4] = {' ','S','T','M'};
//static const uint8_t g1l_header[8] = {'G', '1','L','_','0','0','0','0'};
//static const uint8_t bgm_header[6] = {'W','i','i','B','G','M'};


static const uint8_t head_name[4] = {'H','E','A','D'};
static const uint8_t info_name[4] = {'I','N','F','O'};

static const uint8_t adpc_name[4] = {'A','D','P','C'};
static const uint8_t seek_name[4] = {'S','E','E','K'};

static const uint8_t data_name[4] = {'D','A','T','A'};



static void usage(void)
{
    fprintf(stderr,
            "Cafe Citra B\n"
            "Version " VERSION " (built " __DATE__ ")\n\n"
            "Build .bcstm or .bfstm files from mono .dsp\n"
            "build usage:\n"
            "    %s --build-brstm dest.brstm source.dsp [sourceR.dsp%s] [options]\n"
			"    %s --build-bcstm dest.bcstm source.dsp [sourceR.dsp%s] [options]\n"
			"    %s --build-bfstm dest.bfstm source.dsp [sourceR.dsp%s] [options]\n"
//			"    %s --build-idsp dest.idsp source.dsp [sourceR.dsp%s] [options]\n"
			"  --volume-pan-chunk\n"
            "  --alternate-adpc-count\n"
            "\n",
            bin_name, (MAX_CHANNELS > 2 ? " ..." : ""), bin_name, (MAX_CHANNELS > 2 ? " ..." : ""), bin_name, (MAX_CHANNELS > 2 ? " ..." : ""));

    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    const char *brstm_name = NULL;
    const char *dsp_names[MAX_CHANNELS];
    enum revb_mode mode = MODE_INVALID;
	enum output_strm strm = 1; 
    enum odd_options options = 0;
    int dsp_count = 0;
	
	

    /* for usage() */
    bin_name = argv[0];

    /* clear dsp_names */
    for (int i = 0; i < MAX_CHANNELS; i++) dsp_names[i] = NULL;

    /* process arguments */
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp("--build-brstm", argv[i]))
        {
            if (mode != MODE_INVALID) usage();
            if (i >= argc-1) usage();
			
            mode = MODE_BUILD;
            brstm_name = argv[++i];
        }
		
		else if (!strcmp("--build-bcstm", argv[i]))
        {
            if (mode != MODE_INVALID) usage();
            if (i >= argc-1) usage();

			strm = 2;
			memcpy(STRM_sig, CSTM_sig, 4);
			put_16 = put_16_le;
			put_16_seek = put_16_le_seek;
			put_32 = put_32_le;
			put_32_seek = put_32_le_seek;
            mode = MODE_BUILD;
            brstm_name = argv[++i];
        }
		
		else if (!strcmp("--build-bfstm", argv[i]))
        {
            if (mode != MODE_INVALID) usage();
            if (i >= argc-1) usage();
			
			strm = 3;
			memcpy(STRM_sig, FSTM_sig, 4);
            mode = MODE_BUILD;
            brstm_name = argv[++i];
        }
		
		else if (!strcmp("--build-idsp", argv[i]))
        {
            if (mode != MODE_INVALID) usage();
            if (i >= argc-1) usage();
			
			strm = 4;
            mode = MODE_BUILD;
            brstm_name = argv[++i];
        }
		
		else if (!strcmp("--build-g1l", argv[i]))
        {
            if (mode != MODE_INVALID) usage();
            if (i >= argc-1) usage();
			
			strm = 5;
            mode = MODE_BUILD;
            brstm_name = argv[++i];
        }
		
        else if (!strcmp("--volume-pan-chunk", argv[i]))
        {
            if (options & OPTION_SECOND_CHUNK_EXTRA) usage();
            options |= OPTION_SECOND_CHUNK_EXTRA;
        }
		
        else if (!strcmp("--alternate-adpc-count", argv[i]))
        {
            if (options & OPTION_ALTERNATE_ADPC_COUNT) usage();
            options |= OPTION_ALTERNATE_ADPC_COUNT;
        }
        else
        {
            if (dsp_count == MAX_CHANNELS)
            {
                fprintf(stderr,"Maximum of %d channels.\n",MAX_CHANNELS);
                exit(EXIT_FAILURE);
            }
            dsp_names[dsp_count++] = argv[i];
        }
    }

    /* some additional mode checks */
    if (mode != MODE_BUILD && options != 0) usage();
    if (mode == MODE_EXAMINE && dsp_count != 0) usage();
    if (mode != MODE_EXAMINE && dsp_count == 0) usage();

    switch (mode)
    {
        case MODE_BUILD:
            if (dsp_count > 12)
            {
                fprintf(stderr, "Not comfortable with building > 12 channels\n");
                exit(EXIT_FAILURE);
            }
			switch (strm) {
				case BRSTM:
					build_brstm(brstm_name, dsp_names, dsp_count, options);
				break;
				case BCSTM:case BFSTM:
					build(brstm_name, dsp_names, dsp_count, options, strm);
				break;
				case SSB_IDSP:
					build_idsp(brstm_name, dsp_names, dsp_count, options);
				break;
				case TECMO_G1l:
					build_g1l(brstm_name, dsp_names, dsp_count, options);
				break;
			}
            break;
        case MODE_EXAMINE:
        case MODE_EXTRACT:
            break;
        case MODE_INVALID:
            usage();
            break;
    }

    exit(EXIT_SUCCESS);
}

static void expect_8(uint8_t expected, long offset, const char *desc,
        FILE *infile)
{
    uint8_t found = get_byte_seek(offset, infile);
    if (found != expected)
    {
        fprintf(stderr,"expected 0x%02"PRIx8" at offset 0x%lx (%s), "
                "found 0x%02"PRIx8"\n",
                expected, (unsigned long)offset, desc, found);
        exit(EXIT_FAILURE);
    }
}
static void expect_16(uint16_t expected, long offset, const char *desc,
        FILE *infile)
{
    uint16_t found = get_16_be_seek(offset, infile);
    if (found != expected)
    {
        fprintf(stderr,"expected 0x%04"PRIx16" at offset 0x%lx (%s), "
                "found 0x%04"PRIx16"\n",
                expected, (unsigned long)offset, desc, found);
        exit(EXIT_FAILURE);
    }
}

static void expect_32(uint32_t expected, long offset, const char *desc,
        FILE *infile)
{
    uint32_t found = get_32_be_seek(offset, infile);
    if (found != expected)
    {
        fprintf(stderr,"expected 0x%08"PRIx32" at offset 0x%lx (%s), "
                "found 0x%08"PRIx32"\n",
                expected, (unsigned long)offset, desc, found);
        exit(EXIT_FAILURE);
    }
}

static uint32_t samples_to_nibbles(uint32_t samples)
{
    uint32_t nibbles = samples / 14 * 16;
    if (samples % 14) nibbles += 2 + samples % 14;
    return nibbles;
}

static uint32_t nibbles_to_samples(uint32_t nibbles)
{
    uint32_t whole_frames = nibbles / 16;
    if (nibbles % 16)
    {
        return whole_frames * 14 + nibbles % 16 - 2;
    }
    return whole_frames * 14;
}


void build(const char *brstm_name, const char *dsp_names[],
        int dsp_count, enum odd_options options, enum output_strm strm)
{
    FILE *outfile = NULL;
    FILE *infiles[MAX_CHANNELS];

    for (int i = 0; i < MAX_CHANNELS; i++)
    {
        infiles[i] = NULL;
    }

    /* announce intentions */
    fprintf(stderr, "Building %s from:\n",brstm_name);
    for (int i = 0; i < dsp_count; i++)
    {
        fprintf(stderr, "  channel %d: %s\n", i, dsp_names[i]);
    }

    fprintf(stderr,"\n");

    /* open input files */
    for (int i = 0; i < dsp_count; i++)
    {
        infiles[i] = fopen(dsp_names[i], "rb");
        CHECK_ERRNO(infiles[i] == NULL, "fopen of input file");
    }

    /* open output file */
    outfile = fopen(brstm_name, "wb");
    CHECK_ERRNO(outfile == NULL, "fopen of output file");

    /* check that the DSPs agree */
    uint8_t dsp_header0[0x60] = {0};

    get_bytes_seek(0, infiles[0], dsp_header0, 0x1c);
    for (int i = 1; i < dsp_count; i++)
    {
        uint8_t dsp_header1[0x1c];
        get_bytes_seek(0, infiles[i], dsp_header1, 0x1c);
        if (memcmp(dsp_header0, dsp_header1, 0x1c))
        {
            fprintf(stderr, "DSP headers do not agree\n");
            exit(EXIT_FAILURE);
        }
    }

    /* read important elements of the header */
    uint32_t nibble_count = read_32_be(dsp_header0 + 4);
    const uint32_t sample_rate = read_32_be(dsp_header0 + 8);
    const uint16_t loop_flag = read_16_be(dsp_header0 + 0xc);
    if (read_16_be(dsp_header0 + 0xe) != 0)
    {
        fprintf(stderr, "source file is not DSP ADPCM\n");
        exit(EXIT_FAILURE);
    }
    const uint32_t loop_nibble = read_32_be(dsp_header0 + 0x10);

    /* truncate to loop end (note that standard DSP loop end is the
     * last nibble played, not one after) */
    if (loop_flag && read_32_be(dsp_header0 + 0x14)+1 < nibble_count)
        nibble_count = read_32_be(dsp_header0 + 0x14)+1;
    const uint32_t sample_count = nibbles_to_samples(nibble_count);

    /* now we can start building the file */
    uint32_t current_offset = 0;
    
    /* begin RSTM header */
	
    put_bytes_seek(current_offset, outfile, STRM_sig, 4);
    put_16(0xFEFF, outfile);	
	put_16(0x0040, outfile);
	switch(strm) {
		case BFSTM:
			put_16(3, outfile);
			put_16(0, outfile);
		break;
		case BCSTM:
			put_16(0, outfile);
			put_16_be(2, outfile);
		break;
	}
    current_offset += 12;
    /* reserve space for file size */
    const uint32_t file_size_offset = current_offset;
    current_offset += 4;
	
    /* header size and ?? */
	put_16_seek(3, current_offset, outfile);
    put_16(0, outfile);
    put_16(0x4000, outfile);
	put_16(0, outfile);
    current_offset += 4;
    /* reserve space for chunk info */
    const uint32_t head_chunk_offset_offset = 0x18;
    const uint32_t head_chunk_size_offset = 0x1c;
    put_16_seek(0x4001, 0x20, outfile);
	put_16(0, outfile);
    const uint32_t adpc_chunk_offset_offset = 0x24;
    const uint32_t adpc_chunk_size_offset = 0x28;
    put_16_seek(0x4002, 0x2c, outfile);
	put_16(0, outfile);
    const uint32_t data_chunk_offset_offset = 0x30;
    const uint32_t data_chunk_size_offset = 0x34;
    current_offset += 8;

    /* pad */
    current_offset = pad(0x38, 0x20, outfile);

    /* begin HEAD chunk header */
    const uint32_t head_chunk_offset = 0x40;
    put_32_seek(head_chunk_offset, head_chunk_offset_offset, outfile);
    put_bytes_seek(current_offset, outfile, info_name, 4);
    current_offset += 8;

    /* HEAD body */
    const uint32_t head_offset = current_offset;
    const uint32_t block_size = 0x2000;
    uint32_t last_block_size;
    uint32_t last_block_used_bytes;
    uint32_t block_count;
    uint32_t head_data_offset_offset;
    uint32_t samples_per_adpc_entry;
    uint32_t bytes_per_adpc_entry;
    {
        /* chunk list */
        put_16_seek(0x4100, head_chunk_offset + 8, outfile);
		put_16(0, outfile);
		put_32(0x18, outfile);
		put_16(0x101, outfile);
		put_16(0, outfile);
		put_32(0x50, outfile);
		put_16(0x101, outfile);
		put_16(0, outfile);
		put_32(0x54 + ceil((double)dsp_count/2) * 8, outfile);
		current_offset = 0x60;
		
        put_byte_seek(2, current_offset, outfile);  /* DSP ADPCM */
        put_byte((loop_flag != 0),  outfile);
        put_byte(dsp_count, outfile);
        put_byte(0, outfile);   /* padding */
		
        put_32(sample_rate, outfile);
        put_32(nibbles_to_samples(loop_nibble), outfile);    /* loop start */
        put_32(sample_count, outfile);
        current_offset += 0x10;

//        head_data_offset_offset = current_offset;
//        current_offset += 4;

        /* DSP-centric */
        const uint32_t samples_per_block = nibbles_to_samples(block_size*2);
        if ( nibbles_to_samples(loop_nibble) % samples_per_block != 0 )
        {
            fprintf(stderr, "Warning!\n"
                    "   Loop start sample %" PRIu32 " is not on "
                    "a block boundary.\n"
                    "   The brstm may not loop properly "
                    "(blocks are %" PRIu32 " samples).\n"
                    "   This can be solved by adding %" PRIu32 " samples of\n"
                    "   silence to the beginning of the track.\n\n",
                    nibbles_to_samples(loop_nibble), samples_per_block,
                    samples_per_block -
                    (nibbles_to_samples(loop_nibble) % samples_per_block)
                    );
        }
        block_count = (sample_count + samples_per_block-1) / samples_per_block;
        put_32_seek(block_count, current_offset, outfile);
        put_32(block_size, outfile);
        put_32(samples_per_block, outfile);
        const uint32_t last_block_samples = sample_count % samples_per_block;
        last_block_used_bytes = (samples_to_nibbles(last_block_samples)+1)/2;
        last_block_size = (last_block_used_bytes + 0x1f) / 0x20 * 0x20;
        put_32(last_block_used_bytes, outfile);
        put_32(last_block_samples, outfile);
        put_32(last_block_size, outfile);

        if (options & OPTION_ALTERNATE_ADPC_COUNT)
            samples_per_adpc_entry = 0x400;
        else
            samples_per_adpc_entry = samples_per_block;
		bytes_per_adpc_entry = 4;
        put_32(bytes_per_adpc_entry, outfile);
        put_32(samples_per_adpc_entry, outfile);
        current_offset += 0x20;

        put_16_seek(0x1f00, current_offset, outfile);
        put_16(0, outfile);
        put_32(0x18, outfile);
        put_32_le(ceil((double)dsp_count/2), outfile);
		
		current_offset += 12;
		
		for (int i = 0; i < ceil((double)dsp_count/2); i++) {
			put_16(0x4101, outfile);
			put_16(0, outfile);
			put_32(dsp_count * 12 + 8 + i * 0x14, outfile);
			current_offset += 8;
		}
		put_32(dsp_count, outfile);
		current_offset += 4;
		for (int i = 0; i < dsp_count; i++) {
			put_16(0x4102, outfile);
			put_16(0, outfile);
			put_32(4 + dsp_count * 8 + ceil((double)dsp_count/2) * 0x14 + i * 8, outfile);
			current_offset += 8;
		}
		
		// Writing volume, pan, channel info
		for (int i = 0; i < ceil((double)dsp_count/2); i++) {
			put_byte(127, outfile);
			put_byte(64, outfile);
			put_16(0, outfile);
			put_32(0x100, outfile);
			put_32(0xc, outfile);
			if (2*i+1 == dsp_count)
				put_32(1, outfile);
			else
				put_32(2, outfile);
			put_byte(2*i, outfile);
			put_byte(2*i + 1, outfile);
			put_16(0, outfile);
			current_offset += 0x14;
		}
		
		for (int i = 0; i < dsp_count; i++) {
			put_16(0x300, outfile);
			put_16(0, outfile);
			put_32(dsp_count * 8 + i * 0x26, outfile);
			current_offset += 8;
		}
		
//        current_offset = 0xdc;
   
        for (int i = 0; i < dsp_count; i++)
        {			
            uint16_t dsp_header_coeffs[16];
            uint16_t dsp_header_channel_info[7];
			fseek(infiles[i], 0x1c, SEEK_SET);
			for (int j = 0; j < 16; j++){
				dsp_header_coeffs[j] = get_16_be(infiles[i]);
				put_16(dsp_header_coeffs[j], outfile);
			}
			fseek(infiles[i], 2, SEEK_CUR);
			for (int j = 0; j < 7; j++){
				dsp_header_channel_info[j] = get_16_be(infiles[i]);
				put_16(dsp_header_channel_info[j], outfile);
			}
            current_offset += 0x2e;
        }

        /* pad */
        current_offset = pad(current_offset, 0x20, outfile);

        /* done with HEAD chunk, store size */
        put_32_seek(current_offset-head_chunk_offset,head_chunk_size_offset,
                outfile);
        put_32_seek(current_offset-head_chunk_offset,head_chunk_offset+4,
                outfile);
    } /* end write HEAD chunk */

    /* begin ADPC chunk header */
    const uint32_t adpc_chunk_offset = current_offset;
    put_32_seek(adpc_chunk_offset, adpc_chunk_offset_offset, outfile);
    put_bytes_seek(current_offset, outfile, seek_name, 4);
    current_offset += 8;

    /* ADPC body */
    {
        uint32_t adpc_blocks;
        if (options & OPTION_ALTERNATE_ADPC_COUNT)
        {
            adpc_blocks = (block_size * (block_count-1) + last_block_size) /
                0x400 + 1;
        }
        else
        {
            adpc_blocks = sample_count / samples_per_adpc_entry + 1;
        }

        /* TODO actually fill in ADPC values */
        current_offset +=
            adpc_blocks * bytes_per_adpc_entry * dsp_count;

        /* pad */
        current_offset = pad(current_offset, 0x20, outfile);

        /* done with ADPC chunk, store size */
        put_32_seek(current_offset-adpc_chunk_offset,adpc_chunk_size_offset,
                outfile);
        put_32_seek(current_offset-adpc_chunk_offset,adpc_chunk_offset+4,
                outfile);
    } /* end write ADPC chunk */

    /* begin DATA chunk header */
    const uint32_t data_chunk_offset = current_offset;
    put_32_seek(data_chunk_offset, data_chunk_offset_offset, outfile);
    put_bytes_seek(current_offset, outfile, data_name, 4);
    current_offset += 8;

    /* DATA body */
    {
        uint32_t infile_offset = 0x60;
        CHECK_ERRNO(fseek(outfile, current_offset, SEEK_SET) != 0, "fseek");

        put_32(0, outfile);
        current_offset += 0x18;
//        put_32_seek(current_offset, head_data_offset_offset, outfile);
        CHECK_ERRNO(fseek(outfile, current_offset, SEEK_SET) != 0, "fseek");

        for (uint32_t i = 0; i < block_count; i++)
        {
            if (i == block_count-1 && last_block_size) break;

            for (int channel = 0; channel < dsp_count; channel++)
            {
                dump(infiles[channel], outfile, infile_offset, block_size);
                current_offset += block_size;
            }

            infile_offset += block_size;
        }
        if (last_block_size)
        {
            for (int channel = 0; channel < dsp_count; channel++)
            {
                CHECK_ERRNO(fseek(outfile, current_offset, SEEK_SET) != 0,
                        "fseek");
                dump(infiles[channel], outfile, infile_offset,
                        last_block_used_bytes);

                /* pad */
                for (uint32_t i = last_block_used_bytes; i < last_block_size;
                        i++)
                {
                    put_byte(0, outfile);
                }
                current_offset += last_block_size;
            }
        }

        /* pad */
        current_offset = pad(current_offset, 0x20, outfile);

        /* done with DATA chunk, store size */
        put_32_seek(current_offset-data_chunk_offset,data_chunk_size_offset,
                outfile);
        put_32_seek(current_offset-data_chunk_offset,data_chunk_offset+4,
                outfile);
    } /* end write DATA chunk */

    /* done with file */
    put_32_seek(current_offset, file_size_offset, outfile);

    /* close files */
    for (int i = 0; i < dsp_count; i++)
    {
        CHECK_ERRNO(fclose(infiles[i]) != 0, "fclose");
        infiles[i] = NULL;
    }
    CHECK_ERRNO(fclose(outfile) != 0, "fclose");
    outfile = NULL;

    fprintf(stderr, "Done!\n");
}

void build_brstm(const char *brstm_name, const char *dsp_names[],
        int dsp_count, enum odd_options options)
{
    FILE *outfile = NULL;
    FILE *infiles[MAX_CHANNELS];

    for (int i = 0; i < MAX_CHANNELS; i++)
    {
        infiles[i] = NULL;
    }

    /* announce intentions */
    fprintf(stderr, "Building %s from:\n",brstm_name);
    for (int i = 0; i < dsp_count; i++)
    {
        fprintf(stderr, "  channel %d: %s\n", i, dsp_names[i]);
    }

    fprintf(stderr,"\n");

    /* open input files */
    for (int i = 0; i < dsp_count; i++)
    {
        infiles[i] = fopen(dsp_names[i], "rb");
        CHECK_ERRNO(infiles[i] == NULL, "fopen of input file");
    }

    /* open output file */
    outfile = fopen(brstm_name, "wb");
    CHECK_ERRNO(outfile == NULL, "fopen of output file");

    /* check that the DSPs agree */
    uint8_t dsp_header0[0x60] = {0};

    get_bytes_seek(0, infiles[0], dsp_header0, 0x1c);
    for (int i = 1; i < dsp_count; i++)
    {
        uint8_t dsp_header1[0x1c];
        get_bytes_seek(0, infiles[i], dsp_header1, 0x1c);
        if (memcmp(dsp_header0, dsp_header1, 0x1c))
        {
            fprintf(stderr, "DSP headers do not agree\n");
            exit(EXIT_FAILURE);
        }
    }

    /* read important elements of the header */
    uint32_t nibble_count = read_32_be(dsp_header0 + 4);
    const uint32_t sample_rate = read_32_be(dsp_header0 + 8);
    const uint16_t loop_flag = read_16_be(dsp_header0 + 0xc);
    if (read_16_be(dsp_header0 + 0xe) != 0)
    {
        fprintf(stderr, "source file is not DSP ADPCM\n");
        exit(EXIT_FAILURE);
    }
    const uint32_t loop_nibble = read_32_be(dsp_header0 + 0x10);

    /* truncate to loop end (note that standard DSP loop end is the
     * last nibble played, not one after) */
    if (loop_flag && read_32_be(dsp_header0 + 0x14)+1 < nibble_count)
        nibble_count = read_32_be(dsp_header0 + 0x14)+1;
    const uint32_t sample_count = nibbles_to_samples(nibble_count);

    /* now we can start building the file */
    uint32_t current_offset = 0;
    
    /* begin RSTM header */
	
    put_bytes_seek(current_offset, outfile, RSTM_sig, 4);
    put_32_be(0xFEFF0100, outfile);
    current_offset += 8;
    /* reserve space for file size */
    const uint32_t file_size_offset = current_offset;
    current_offset += 4;
    /* header size and ?? */
    put_16_be_seek(0x40, current_offset, outfile);
    put_16_be(0x2, outfile);
    current_offset += 4;
    /* reserve space for chunk info */
    const uint32_t head_chunk_offset_offset = current_offset;
    const uint32_t head_chunk_size_offset = current_offset + 4;
    current_offset += 8;
    const uint32_t adpc_chunk_offset_offset = current_offset;
    const uint32_t adpc_chunk_size_offset = current_offset + 4;
    current_offset += 8;
    const uint32_t data_chunk_offset_offset = current_offset;
    const uint32_t data_chunk_size_offset = current_offset + 4;
    current_offset += 8;

    /* pad */
    current_offset = pad(current_offset, 0x20, outfile);

    /* begin HEAD chunk header */
    const uint32_t head_chunk_offset = current_offset;
    put_32_be_seek(head_chunk_offset, head_chunk_offset_offset, outfile);
    put_bytes_seek(current_offset, outfile, head_name, 4);
    current_offset += 8;

    /* HEAD body */
    const uint32_t head_offset = current_offset;
    const uint32_t block_size = 0x2000;
    uint32_t last_block_size;
    uint32_t last_block_used_bytes;
    uint32_t block_count;
    uint32_t head_data_offset_offset;
    uint32_t samples_per_adpc_entry;
    uint32_t bytes_per_adpc_entry;
    {
        /* chunk list */
        put_32_be_seek(0x01000000, current_offset, outfile);
        const uint32_t first_chunk_offset_offset = current_offset + 4;
        current_offset += 8;
        put_32_be_seek(0x01000000, current_offset, outfile);
        const uint32_t second_chunk_offset_offset = current_offset + 4;
        current_offset += 8;
        put_32_be_seek(0x01000000, current_offset, outfile);
        const uint32_t third_chunk_offset_offset = current_offset + 4;
        current_offset += 8;

        /* first chunk */
        put_32_be_seek(current_offset-head_offset, first_chunk_offset_offset,
                outfile);
        put_byte_seek(2, current_offset, outfile);  /* DSP ADPCM */
        put_byte((loop_flag != 0),  outfile);
        put_byte(dsp_count, outfile);
        put_byte(0, outfile);   /* padding */
        put_16_be(sample_rate, outfile);
        put_16_be(0, outfile);  /* padding */
        put_32_be(nibbles_to_samples(loop_nibble), outfile);    /* loop start */
        put_32_be(sample_count, outfile);
        current_offset += 0x10;

        head_data_offset_offset = current_offset;
        current_offset += 4;

        /* DSP-centric */
        const uint32_t samples_per_block = nibbles_to_samples(block_size*2);
        if ( nibbles_to_samples(loop_nibble) % samples_per_block != 0 )
        {
            fprintf(stderr, "Warning!\n"
                    "   Loop start sample %" PRIu32 " is not on "
                    "a block boundary.\n"
                    "   The brstm may not loop properly "
                    "(blocks are %" PRIu32 " samples).\n"
                    "   This can be solved by adding %" PRIu32 " samples of\n"
                    "   silence to the beginning of the track.\n\n",
                    nibbles_to_samples(loop_nibble), samples_per_block,
                    samples_per_block -
                    (nibbles_to_samples(loop_nibble) % samples_per_block)
                    );
        }
        block_count = (sample_count + samples_per_block-1) / samples_per_block;
        put_32_be_seek(block_count, current_offset, outfile);
        put_32_be(block_size, outfile);
        put_32_be(samples_per_block, outfile);
        const uint32_t last_block_samples = sample_count % samples_per_block;
        last_block_used_bytes = (samples_to_nibbles(last_block_samples)+1)/2;
        last_block_size = (last_block_used_bytes + 0x1f) / 0x20 * 0x20;
        put_32_be(last_block_used_bytes, outfile);
        put_32_be(last_block_samples, outfile);
        put_32_be(last_block_size, outfile);

        if (options & OPTION_ALTERNATE_ADPC_COUNT)
            samples_per_adpc_entry = 0x400;
        else
            samples_per_adpc_entry = samples_per_block;
        put_32_be(samples_per_adpc_entry, outfile);
        bytes_per_adpc_entry = 4;
        put_32_be(bytes_per_adpc_entry, outfile);
        current_offset += 0x20;

        /* volume, pan, and channel pairing chunk */
        put_32_be_seek(current_offset-head_offset, second_chunk_offset_offset,
                outfile);
        if (options & OPTION_SECOND_CHUNK_EXTRA)
        {
			put_byte_seek(ceil((double)dsp_count/2), current_offset, outfile);
//			printf("%.02f channel pairs for %d channels\n", ceil((double)dsp_count/2), dsp_count);
			put_byte(1, outfile);
			put_16_be(0, outfile);
			current_offset += 4;
			for (int i = 0; i < ceil((double)dsp_count/2); i++) {
				put_32_be(0x01010000, outfile);
				put_32_be(0x50 + ceil((double)dsp_count/2) * 8 + i * 0xc, outfile);
				current_offset += 8;
			}
			
			for (int i = 0; i < ceil((double)dsp_count/2); i++) {
				put_byte(127, outfile); // max volume 
				put_byte(64, outfile); // centered pan
				put_16_be(0, outfile);	// padding
				put_32_be(0, outfile);
				if (i*2 == dsp_count - 1) {
					put_byte(1, outfile);	// channels per pair
					put_byte(i * 2, outfile);
					put_byte(0, outfile);
				}
				else {
					put_byte(2, outfile);	// channels per pair (1 channel sometimes)
					put_byte(i * 2, outfile);
					put_byte(i * 2 + 1, outfile);
				}
					
				put_byte(0, outfile);
				current_offset += 0xc;
			}
       
	   }
        else
        {
            put_32_be_seek(0x01000000, current_offset, outfile);
            put_32_be(0x01000000, outfile);
            put_32_be(0x58, outfile);
            if (dsp_count == 1)
                put_32_be(0x01000000, outfile);
            else
                put_32_be(0x02000100, outfile);
            current_offset += 0x10;
        }

        /* third chunk */
        put_32_be_seek(current_offset-head_offset, third_chunk_offset_offset,
                outfile);
        put_byte_seek(dsp_count, current_offset, outfile);
        put_bytes(outfile, (const uint8_t *)"\0\0", 3);   /* padding */
        current_offset += 4;
        uint32_t channel_info_offset_3x[MAX_CHANNELS];
        for (int i = 0; i < dsp_count; i++)
        {
            put_32_be_seek(0x01000000, current_offset, outfile);
            current_offset+=4;
            channel_info_offset_3x[i] = current_offset;
            current_offset+=4;
        }
        uint32_t channel_info_offset_2x[MAX_CHANNELS];
        for (int i = 0; i < dsp_count; i++)
        {
            put_32_be_seek(current_offset-head_offset,
                    channel_info_offset_3x[i], outfile);
            put_32_be_seek(0x01000000, current_offset, outfile);
            current_offset+=4;
            channel_info_offset_2x[i] = current_offset;
            current_offset+=4;

            put_32_be_seek(current_offset-head_offset,
                    channel_info_offset_2x[i], outfile);
            uint8_t dsp_header_channel_info[0x30];
            get_bytes_seek(0x1c, infiles[i], dsp_header_channel_info, 0x30);
            put_bytes(outfile, dsp_header_channel_info, 0x30);
            current_offset += 0x30;
        }

        /* pad */
        current_offset = pad(current_offset, 0x20, outfile);

        /* done with HEAD chunk, store size */
        put_32_be_seek(current_offset-head_chunk_offset,head_chunk_size_offset,
                outfile);
        put_32_be_seek(current_offset-head_chunk_offset,head_chunk_offset+4,
                outfile);
    } /* end write HEAD chunk */

    /* begin ADPC chunk header */
    const uint32_t adpc_chunk_offset = current_offset;
    put_32_be_seek(adpc_chunk_offset, adpc_chunk_offset_offset, outfile);
    put_bytes_seek(current_offset, outfile, adpc_name, 4);
    current_offset += 8;

    /* ADPC body */
    {
        uint32_t adpc_blocks;
        if (options & OPTION_ALTERNATE_ADPC_COUNT)
        {
            adpc_blocks = (block_size * (block_count-1) + last_block_size) /
                0x400 + 1;
        }
        else
        {
            adpc_blocks = sample_count / samples_per_adpc_entry + 1;
        }

        /* TODO actually fill in ADPC values */
        current_offset +=
            adpc_blocks * bytes_per_adpc_entry * dsp_count;

        /* pad */
        current_offset = pad(current_offset, 0x20, outfile);

        /* done with ADPC chunk, store size */
        put_32_be_seek(current_offset-adpc_chunk_offset,adpc_chunk_size_offset,
                outfile);
        put_32_be_seek(current_offset-adpc_chunk_offset,adpc_chunk_offset+4,
                outfile);
    } /* end write ADPC chunk */

    /* begin DATA chunk header */
    const uint32_t data_chunk_offset = current_offset;
    put_32_be_seek(data_chunk_offset, data_chunk_offset_offset, outfile);
    put_bytes_seek(current_offset, outfile, data_name, 4);
    current_offset += 8;

    /* DATA body */
    {
        uint32_t infile_offset = 0x60;
        CHECK_ERRNO(fseek(outfile, current_offset, SEEK_SET) != 0, "fseek");

        put_32_be(0x18, outfile);
        current_offset += 0x18;
        put_32_be_seek(current_offset, head_data_offset_offset, outfile);
        CHECK_ERRNO(fseek(outfile, current_offset, SEEK_SET) != 0, "fseek");

        for (uint32_t i = 0; i < block_count; i++)
        {
            if (i == block_count-1 && last_block_size) break;

            for (int channel = 0; channel < dsp_count; channel++)
            {
                dump(infiles[channel], outfile, infile_offset, block_size);
                current_offset += block_size;
            }

            infile_offset += block_size;
        }
        if (last_block_size)
        {
            for (int channel = 0; channel < dsp_count; channel++)
            {
                CHECK_ERRNO(fseek(outfile, current_offset, SEEK_SET) != 0,
                        "fseek");
                dump(infiles[channel], outfile, infile_offset,
                        last_block_used_bytes);

                /* pad */
                for (uint32_t i = last_block_used_bytes; i < last_block_size;
                        i++)
                {
                    put_byte(0, outfile);
                }
                current_offset += last_block_size;
            }
        }

        /* pad */
        current_offset = pad(current_offset, 0x20, outfile);

        /* done with DATA chunk, store size */
        put_32_be_seek(current_offset-data_chunk_offset,data_chunk_size_offset,
                outfile);
        put_32_be_seek(current_offset-data_chunk_offset,data_chunk_offset+4,
                outfile);
    } /* end write DATA chunk */

    /* done with file */
    put_32_be_seek(current_offset, file_size_offset, outfile);

    /* close files */
    for (int i = 0; i < dsp_count; i++)
    {
        CHECK_ERRNO(fclose(infiles[i]) != 0, "fclose");
        infiles[i] = NULL;
    }
    CHECK_ERRNO(fclose(outfile) != 0, "fclose");
    outfile = NULL;

    fprintf(stderr, "Done!\n");
}

void build_g1l(const char *brstm_name, const char *dsp_names[], int dsp_count, enum odd_options options) {
	FILE *outfile = NULL;
    FILE *infiles[MAX_CHANNELS];

    for (int i = 0; i < MAX_CHANNELS; i++)
    {
        infiles[i] = NULL;
    }

    /* announce intentions */
    fprintf(stderr, "Building %s from:\n",brstm_name);
    for (int i = 0; i < dsp_count; i++)
    {
        fprintf(stderr, "  channel %d: %s\n", i, dsp_names[i]);
    }

    fprintf(stderr,"\n");

    /* open input files */
    for (int i = 0; i < dsp_count; i++)
    {
        infiles[i] = fopen(dsp_names[i], "rb");
        CHECK_ERRNO(infiles[i] == NULL, "fopen of input file");
    }

    /* open output file */
    outfile = fopen(brstm_name, "wb");
    CHECK_ERRNO(outfile == NULL, "fopen of output file");

    /* check that the DSPs agree */
    uint8_t dsp_header0[0x60] = {0};

    get_bytes_seek(0, infiles[0], dsp_header0, 0x1c);
    for (int i = 1; i < dsp_count; i++)
    {
        uint8_t dsp_header1[0x1c];
        get_bytes_seek(0, infiles[i], dsp_header1, 0x1c);
        if (memcmp(dsp_header0, dsp_header1, 0x1c))
        {
            fprintf(stderr, "DSP headers do not agree\n");
            exit(EXIT_FAILURE);
        }
    }

    /* read important elements of the header */
    uint32_t nibble_count = read_32_be(dsp_header0 + 4);
    const uint32_t sample_rate = read_32_be(dsp_header0 + 8);
    const uint16_t loop_flag = read_16_be(dsp_header0 + 0xc);
    if (read_16_be(dsp_header0 + 0xe) != 0)
    {
        fprintf(stderr, "source file is not DSP ADPCM\n");
        exit(EXIT_FAILURE);
    }
    const uint32_t loop_nibble = read_32_be(dsp_header0 + 0x10);

    /* truncate to loop end (note that standard DSP loop end is the
     * last nibble played, not one after) */
    if (loop_flag && read_32_be(dsp_header0 + 0x14)+1 < nibble_count)
        nibble_count = read_32_be(dsp_header0 + 0x14)+1;
    const uint32_t sample_count = nibbles_to_samples(nibble_count);

    /* now we can start building the file */
    uint32_t current_offset = 0;
    
    /* begin G1L header */
	const unsigned char * g1l_header = "G1L_0000";
	const unsigned char * bgm_header = "WiiBGM";
    put_bytes_seek(current_offset, outfile, g1l_header, 8);
    put_32_be(0, outfile);
	put_32_be(0x1c, outfile);
	put_32_be(9, outfile);
	put_32_be(1, outfile);
	put_32_be(0x1c, outfile);
	put_bytes(outfile, bgm_header, 6);
	put_16_be(0, outfile);
	put_32_be(0x800, outfile);
	put_32_be(0, outfile);
	put_32_be(sample_count, outfile);
	if (nibbles_to_samples(loop_nibble) > 0)
		put_32_be(nibbles_to_samples(loop_nibble), outfile);
	else if (nibbles_to_samples(loop_nibble) == 0 && loop_flag == 1)
		put_32_be(1, outfile);
	else
		put_32_be(0, outfile);
	put_32_be(0, outfile);
	put_32_be(nibble_count/2, outfile);
	put_32_be(dsp_count, outfile);
	put_32_be(sample_rate, outfile);
	put_32_be(dsp_count/2, outfile);
	for (int i =0; i < 5; i++)
		put_32_be(0, outfile);
	current_offset = 0x5c;
	for (int i = 0; i < dsp_count; i++) {
		uint8_t coeffs[0x30];
		uint32_t loop_end_nibble;
		put_32_be(sample_count, outfile);
		put_32_be(nibble_count, outfile);
		put_32_be(sample_rate, outfile);
		put_32_be(0, outfile);
		put_32_be(2, outfile);
		loop_end_nibble = get_32_be_seek(0x14, infiles[i]);
		put_32_be(loop_end_nibble, outfile);
		put_32_be(2, outfile);
		fseek(infiles[i], 0x1c, SEEK_SET);
		get_bytes_seek(0x1c, infiles[i], coeffs, 0x30);
		put_bytes(outfile, coeffs, 0x30);
		for (int i =0; i < 5; i++)
			put_32_be(0, outfile);
		current_offset += 0x60;
		fseek(infiles[i], 0x60, SEEK_SET);
	}
	while (current_offset < 0x81c) {
		put_32_be(0, outfile);
		current_offset += 4;
	}
	// Writing every DSP 1 byte at a time (1 byte interleave)
	for (int i = 0; i < nibble_count/2; i++) {
		for (int j = 0; j < dsp_count; j++) {
			uint8_t dspByte = get_byte(infiles[j]);
			put_byte(dspByte, outfile);
		}
	}
	
	current_offset = ftell(outfile);
    /* done with file */
    put_32_be_seek(current_offset, 8, outfile);

    /* close files */
    for (int i = 0; i < dsp_count; i++)
    {
        CHECK_ERRNO(fclose(infiles[i]) != 0, "fclose");
        infiles[i] = NULL;
    }
    CHECK_ERRNO(fclose(outfile) != 0, "fclose");
    outfile = NULL;

    fprintf(stderr, "Done!\n");		
}

void build_idsp(const char *brstm_name, const char *dsp_names[], int dsp_count, enum odd_options options) {
	FILE *outfile = NULL;
    FILE *infiles[MAX_CHANNELS];

    for (int i = 0; i < MAX_CHANNELS; i++)
    {
        infiles[i] = NULL;
    }

    /* announce intentions */
    fprintf(stderr, "Building %s from:\n",brstm_name);
	if (dsp_count > 2)
		dsp_count = 2;
    for (int i = 0; i < dsp_count; i++)
    {
        fprintf(stderr, "  channel %d: %s\n", i, dsp_names[i]);
    }

    fprintf(stderr,"\n");

    /* open input files */
    for (int i = 0; i < dsp_count; i++)
    {
        infiles[i] = fopen(dsp_names[i], "rb");
        CHECK_ERRNO(infiles[i] == NULL, "fopen of input file");
    }

    /* open output file */
    outfile = fopen(brstm_name, "wb");
    CHECK_ERRNO(outfile == NULL, "fopen of output file");

    /* check that the DSPs agree */
    uint8_t dsp_header0[0x60] = {0};

    get_bytes_seek(0, infiles[0], dsp_header0, 0x1c);
    for (int i = 1; i < dsp_count; i++)
    {
        uint8_t dsp_header1[0x1c];
        get_bytes_seek(0, infiles[i], dsp_header1, 0x1c);
        if (memcmp(dsp_header0, dsp_header1, 0x1c))
        {
            fprintf(stderr, "DSP headers do not agree\n");
            exit(EXIT_FAILURE);
        }
    }

    /* read important elements of the header */
    uint32_t nibble_count = read_32_be(dsp_header0 + 4);
    const uint32_t sample_rate = read_32_be(dsp_header0 + 8);
    const uint16_t loop_flag = read_16_be(dsp_header0 + 0xc);
    if (read_16_be(dsp_header0 + 0xe) != 0)
    {
        fprintf(stderr, "source file is not DSP ADPCM\n");
        exit(EXIT_FAILURE);
    }
    const uint32_t loop_nibble = read_32_be(dsp_header0 + 0x10);

    /* truncate to loop end (note that standard DSP loop end is the
     * last nibble played, not one after) */
    if (loop_flag && read_32_be(dsp_header0 + 0x14)+1 < nibble_count)
        nibble_count = read_32_be(dsp_header0 + 0x14)+1;
    const uint32_t sample_count = nibbles_to_samples(nibble_count);

    /* now we can start building the file */
    uint32_t current_offset = 0;
    
    /* begin G1L header */
	const unsigned char * idsp_header = "IDSP";
    put_bytes_seek(current_offset, outfile, idsp_header, 4);
    put_32_be(0, outfile);
	put_32_be(dsp_count, outfile);
	put_32_be(sample_rate, outfile);
	put_32_be(sample_count, outfile);
	put_32_be(loop_flag * nibbles_to_samples(loop_nibble), outfile);
	put_32_be(loop_flag * sample_count, outfile);
	put_32_be(0x10, outfile);
	put_32_be(0x40, outfile);
	put_32_be(0x60, outfile);
	put_32_be(0x40 + dsp_count * 0x60, outfile);
	put_32_be(nibble_count/2, outfile);
	for (int i =0; i < 4; i++)
		put_32_be(0, outfile);
	current_offset = 0x5c;
	for (int i = 0; i < dsp_count; i++) {
		uint8_t coeffs[0x30];
		uint32_t loop_end_nibble;
		put_32_be(sample_count, outfile);
		put_32_be(nibble_count, outfile);
		put_32_be(sample_rate, outfile);
		put_16_be(loop_flag, outfile);
		put_16_be(0, outfile);
		put_32_be(loop_nibble, outfile);
		loop_end_nibble = get_32_be_seek(0x14, infiles[i]);
		put_32_be(loop_end_nibble, outfile);
		put_32_be(0, outfile);
		fseek(infiles[i], 0x1c, SEEK_SET);
		get_bytes_seek(0x1c, infiles[i], coeffs, 0x30);
		put_bytes(outfile, coeffs, 0x30);
		for (int i =0; i < 5; i++)
			put_32_be(0, outfile);
		current_offset += 0x60;
		fseek(infiles[i], 0x60, SEEK_SET);
	}
	// Writing every DSP 16 bytes at a time (16 byte interleave)
	for (int i = 0; i < nibble_count/32; i++) {
		for (int j = 0; j < dsp_count; j++) {
			for (int k = 0; k < 4; k++) {
				uint32_t tempData = get_32_be(infiles[j]);
				put_32_be(tempData, outfile);
			}
		}
	}

    /* close files */
    for (int i = 0; i < dsp_count; i++)
    {
        CHECK_ERRNO(fclose(infiles[i]) != 0, "fclose");
        infiles[i] = NULL;
    }
    CHECK_ERRNO(fclose(outfile) != 0, "fclose");
    outfile = NULL;

    fprintf(stderr, "Done!\n");		
}

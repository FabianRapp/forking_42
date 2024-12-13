#include <unistd.h>
#include <immintrin.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "libft_macros.h"
#include <errno.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"

typedef char i8;
typedef unsigned char u8;
typedef unsigned short u16;
typedef int i32;
typedef unsigned u32;
typedef unsigned long u64;

#define HEADER_WIDTH 7
#define HEADER_HEIGHT 8
#define SIMD_ALIGNMENT 16

#define NDEBUG

typedef enum {
	BLUE,
	GREEN,
	RED,
	COUNT,
}	t_colors;

typedef union u_pixel {
	u32		raw;
	struct {
		u8	b;
		u8	g;
		u8	r;
		u8	a;
	}__attribute__((packed));
}	__attribute__((packed)) t_pixel;

#define PRINT_ERROR(cstring) write(STDERR_FILENO, cstring, sizeof(cstring) - 1)

#pragma pack(1)
struct bmp_header {
	// Note: header
	i8  signature[2]; // should equal to "BM"
	u32 file_size;
	u32 unused_0;
	u32 data_offset;
// Note: info header
	u32 info_header_size;
	u32 width; // in px
	u32 height; // in px
	u16 number_of_planes; // should be 1
	u16 bit_per_pixel; // 1, 4, 8, 16, 24 or 32
	u32 compression_type; // should be 0
	u32 compressed_image_size; // should be 0
	// Note: there are more stuff there but it is not important here
};

typedef struct file_content {
	i8*   data;
	u32   size;
}	t_file;

void	print_128_8(__m128i value) {
	uint8_t arr[16];

	_mm_storeu_si128((__m128i*)arr, value);
	printf("__m128i val:");
	for (size_t i = 0; i < sizeof arr / sizeof arr[0]; i++) {
		printf("%x, ", arr[i]);
	}
	printf("\n");
}

/* todo: simpliy allocated memory mangment by removing the header here
	and removing it from the logic later on*/
/* todo: for hure files don't read file in 1 go, read chunks and start threads
	on those chunks before reading more chunks */
static
struct file_content   read_entire_file(char* filename) {
	char* file_data = 0;
	unsigned long	file_size = 0;
	int input_file_fd = open(filename, O_RDONLY);
	if (input_file_fd >= 0) {
		struct stat input_file_stat = {0};
		stat(filename, &input_file_stat);
		if (1) {
			size_t	alloc_size = (input_file_stat.st_size + 4 + 15) & ~((size_t)15);
			struct bmp_header	*header = aligned_alloc(16, alloc_size);
			FT_ASSERT(header);
			file_data = (char *)header;
			read(input_file_fd, header, sizeof *header);
			size_t	trash_len = header->data_offset - sizeof *header;
			read(input_file_fd, header + sizeof *header, trash_len);
			FT_ASSERT(errno == 0);
			while ((uintptr_t)(((char *)header) + header->data_offset) % 16 != 0) {
				header->data_offset++;
			}
			read(input_file_fd, ((char *)header) + header->data_offset, input_file_stat.st_size - sizeof *header);
			close(input_file_fd);
		} else {
			file_size = (unsigned long)input_file_stat.st_size;
			file_data = mmap(0, file_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, input_file_fd, 0);
		}
		FT_ASSERT(errno == 0);
	}
	return (struct file_content){file_data, file_size};
}

static __attribute__((always_inline))
int	possible_header_pixel(t_pixel p) {
	if (p.b == 0x7f && p.g == 0xbc && p.r == 0xd9) {
		return (1);
	}
	return (0);
}

/* called from find_header_start only */
static inline
t_pixel	*bottom_of_L(t_pixel *data, long long row, long long col, long long width) {
	if (row < 7 || col == 0) {
		return (NULL);
	}
	long long left_count = 0;
	while (left_count < 6) {
		if (!possible_header_pixel(data[col - left_count - 1 + row * width])) {
			break ;
		}
		left_count++;
	}
	if (!left_count) {
		return (NULL);
	}
	col -= left_count;
	long long right_count = left_count;
	while (right_count < 6) {
		if (!possible_header_pixel(data[col + right_count + 1 + row * width])) {
			return (NULL);
		}
		right_count++;
	}
	for (long long i = 1; i < 8; i++) {
		if (!possible_header_pixel(data[col + (row - i) * width])) {
			return (NULL);
		}
	}
	return (data + col + row * width);
}

/*todo: this is slow but should only matter for input crafted to breake this*/
/*todo: handle if the input pixel is on the horizontal of the 'L' */
/* to handle crafted input a diffrent dynmaic soltion is needed*/
static __attribute__((always_inline))
t_pixel	*find_header_start(t_pixel *data, long long row, long long col, long long width, long long height) {
	long long	header_height = 1;

	long long input_row = row;

	/* this can be broken, but is not horribly slow */
	/*
	 * example bad input can brake (x is the input to this fn, h are the header pixels):
	 * h000000
	 * h000000
	 * h000000
	 * h000000
	 * h000000
	 * h000000
	 * h000000
	 * x000000
	 * hhhhhhh
	 * h000000
	 * maybe this is handled in some way, I'm not sure
	 */
	while (row >= 0 && header_height < 8) {
		if (!possible_header_pixel(data[col + (row - header_height) * width])) {
			break ;
		}
		header_height++;
	}
	row -= header_height - 1;
	if (row + 7 >= height) {
		return (bottom_of_L(data, input_row, col, width));
	}
	/* adjust this to fix */
	while (header_height < HEADER_HEIGHT) {
		if (!possible_header_pixel(data[col + (row + header_height) * width])) {
			return (bottom_of_L(data, input_row, col, width));
		}
		header_height++;
	}

	row += 7;
	/*and make a loop here while (header_heigh > 7) or somthign smimmliar */
	if (col + 7 < width) {
		for (long long i = 1; i < 7; i++) {
			if (!possible_header_pixel(data[col + i + row * width])) {
				break ;
			}
			if (i == 6 && !possible_header_pixel(data[col + i + 1 + row * width])) {
				return (data + col + row * width);
			}
		}
		row++;
	} else {
	}
	/*and add something like header_height--; goto loop; */
	return (NULL);
}


/* returns offset of src to the first header pixel,
 * -1 if there is no header pixel in the givne n bytes.
 * Assumes src to be 16 byte allinged data.
 * Searched for the 4 byte pattern 0x7bcd9 and the 4th byte anything.
*/
static __attribute__((always_inline))
int8_t	first_header_pixel(t_pixel *src) {
	FT_ASSERT(((uintptr_t)src) % 16 == 0);
	__m128i	mask = _mm_set1_epi32(0x00d9bc7f);//4th byte does not matter
	__m128i	data = _mm_load_si128((__m128i *)(src));//todo: should me load not loadu
	__m128i	cmp8 = _mm_cmpeq_epi8(mask, data);
	int16_t	matches_bit_pattern = _mm_movemask_epi8(cmp8);
	if (matches_bit_pattern) {
		//print_128_8(mask);
		//print_128_8(data);
		//print_128_8(cmp8);
	}
	/* unrolled loop duo to mandetory O0 flag and no other flags allowed */
	if ((matches_bit_pattern & 0b1110) == 0b1110) {
		return (0);
	}
	if ((matches_bit_pattern & 0b11100000) == 0b11100000) {
		return (1);
	}
	if ((matches_bit_pattern & 0b111000000000) == 0b111000000000) {
		return (2);
	}
	if ((matches_bit_pattern & 0b1110000000000000) == 0b1110000000000000) {
		return (3);
	}
	return (-1);
}

/*
pattern to find:

1
1
1
1
1
1
1
1111111

Pattern to check atleast one pixel of each 'L' pattern
(memory pattern, not pattern on the actual img),
x: point to check:
00000000000000000000000000000000
00000000000000000000000000000000
00000000000000000000000000000000
00000000000000000000000000000000
00000000000000000000000000000000
xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
00000000000000000000000000000000
00000000000000000000000000000000
00000000000000000000000000000000
00000000000000000000000000000000
00000000000000000000000000000000
00000000000000000000000000000000
00000000000000000000000000000000
xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
00000000000000000000000000000000
00000000000000000000000000000000
00000000000000000000000000000000
00000000000000000000000000000000
00000000000000000000000000000000
00000000000000000000000000000000
00000000000000000000000000000000
xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
00000000000000000000000000000000
00000000000000000000000000000000
00000000000000000000000000000000
00000000000000000000000000000000
00000000000000000000000000000000
00000000000000000000000000000000
00000000000000000000000000000000
xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
*/

/* acts like the given pixel range are the full image and optizes of that */
t_pixel	*find_header(long long start_row, long long end_row, long long height, t_pixel *data, long long width) {
	FT_ASSERT(sizeof(t_pixel) == 4);
	long long	row = start_row + 7;
	//long long	row = start_row;
	long long	next_end = end_row + HEADER_HEIGHT - (end_row % HEADER_HEIGHT);

	next_end = next_end >= height ? height - 1 : next_end;
	end_row = end_row % HEADER_HEIGHT ? next_end : end_row;
	for (; row <= end_row; row += 8) {
	//for (; row <= end_row; row += 1) {
		FT_ASSERT(row >= 0);
		FT_ASSERT(row < height);
		t_pixel	*cur_row = data + row * width;
		long long col =  0;
		/* alignment, inefficient but not significant */
		while (col < width && 0 != (((uintptr_t) (cur_row + col)) % 16)) {
			if (possible_header_pixel(cur_row[col])) {
				t_pixel	*header = find_header_start(data, row, col, width, height);
				if (header) {
					return (header);
				}
			}
			col++;
		}

		while (col < width) {
			if (col <= width - 4) {
				FT_ASSERT(((uintptr_t)(cur_row + col)) % 16 == 0);
				int8_t	tmp = first_header_pixel(cur_row + col);
				if (tmp != -1) {
					t_pixel	*header = find_header_start(data, row, col + tmp, width, height);
					if (header) {
						return (header);
					}
				}
				col += 4;
			} else {
				if (possible_header_pixel(cur_row[col])) {
					t_pixel	*header = find_header_start(data, row, col, width, height);
					if (header) {
						return (header);
					}
				}
				col++;
			}
		}
		//printf("width: %lld; height: %lld\n", width, height);
		//printf("row: %lld\n", row);
		//printf("col: %lld\n", col);
	}
	return (NULL);
}

typedef struct {
	t_pixel	*data;
	long long width;
	long long height;
	long long start_row;
	long long end_row;
}	t_thread_data;

void	*find_header_thread(void *args) {
	t_thread_data *data = (t_thread_data *)args;
	data->data = find_header(data->start_row, data->end_row, data->height, data->data, data->width);
	return (0);
}

t_pixel	*find_header_threaded(t_pixel *data, long long height, long long width) {
	const u8		thread_count = 10;
	pthread_t		threads[thread_count];
	t_thread_data	thread_data[thread_count];
	long long		rows_per_thread = height / thread_count;
	long long		cur_start = 0;
	
#ifndef NDEBUG
	printf("height: %lld; width: %lld\n", height, width);
#endif
	for (u8 i = 0; i < thread_count; i++) {
		thread_data[i].width = width;
		thread_data[i].height = height;
		thread_data[i].start_row = cur_start;
		cur_start += rows_per_thread;
		thread_data[i].end_row = cur_start - 1;
		thread_data[i].data = data;
		if (i == thread_count - 1) {
			thread_data[i].end_row = height - 1;
		}
		pthread_create(threads + i, 0, find_header_thread,  thread_data + i);
	}
	for (size_t i = 0; i < thread_count; i++) {
		pthread_join(threads[i], 0);
		if (thread_data[i].data) {
			for (size_t j = i + 1; j < thread_count; j++) {
				pthread_join(threads[j], 0);
			}
			return (thread_data[i].data);
		}
	}
	FT_ASSERT(0);
	return (0);
}

static __attribute__((always_inline))
t_pixel	*skip_header(t_pixel *header, size_t width) {
	return (header - width - width + 2);
}

typedef union u_row_data {
	unsigned long long	raw;
	struct {
		t_pixel	a;
		t_pixel	b;
	};
}	 t_row_data;

/*todo: don't allocate here, simply override the img buffer */
void	print_msg_basic(struct bmp_header header, t_file file) {
	t_pixel	*data =  (t_pixel *) (file.data + header.data_offset);
	
	data = find_header_threaded(data, header.height, header.width);
	//data = find_header(0, header.height - 1, data, header.width);
	if (!data) {
		return ;
	}
	uint16_t	len = ((uint16_t)data[7].r) + ((uint16_t)data[7].b);
	data = skip_header(data, header.width);
	size_t	alloc_size = (len + 32 + 15) & ~((size_t)15);
	char	*output = aligned_alloc(16, alloc_size);
	long long row = 0;
	size_t	out_i = 0;

	while (out_i < len) {
		long long col = 0;
		while (col < 6) {
			long long i = -row * header.width + col;
			if (len - out_i >= 6) {
				__m128i val= _mm_loadu_si64(data + i);
				_mm_storeu_si64(output + out_i, val);
				output[out_i + 3] = output[out_i + 4];
				output[out_i + 4] = output[out_i + 5];
				output[out_i + 5] = output[out_i + 6];
				out_i += 6;
				col += 2;
			} else if (len - out_i >= 3) {
				memcpy(output + out_i, data + i, 3);
				out_i += 3;
				col++;
			} else if (len - out_i == 2) {
				memcpy(output + out_i, data + i, 2);
				goto ret;
			} else if (len - out_i == 1) {
				memcpy(output + out_i, data + i, 1);
				goto ret;
			} else if (len - out_i == 0) {
				goto ret;
			}
		}
		row++;
	}
ret:
	write(1, output, len);
}

static __attribute__((always_inline))
void	program(char *path) {

	struct file_content file_content = read_entire_file(path);
	if (file_content.data == NULL) {
		PRINT_ERROR("Failed to read file\n");
	}
	struct bmp_header header = ((struct bmp_header*) file_content.data)[0];

	print_msg_basic(header, file_content);
	free(file_content.data);
}

//#undef NDEBUG
int	main(int argc, char** argv) {
	errno = 0;
	for (int i = 1; i < argc; i++) {

#ifndef NDEBUG
		printf("%s:\n", argv[i]);
#endif
		program(argv[i]);
#ifndef NDEBUG
		printf("\n");
#else
		//return 0;
#endif
	}
	return 0;
}

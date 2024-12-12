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

typedef char i8;
typedef unsigned char u8;
typedef unsigned short u16;
typedef int i32;
typedef unsigned u32;
typedef unsigned long u64;

#define HEADER_WIDTH 7
#define HEADER_HEIGHT 8
#define SIMD_ALIGNMENT 16
//#define NDEBUG

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


struct file_content   read_entire_file(char* filename) {
	char* file_data = 0;
	unsigned long	file_size = 0;
	int input_file_fd = open(filename, O_RDONLY);
	if (input_file_fd >= 0) {
		struct stat input_file_stat = {0};
		stat(filename, &input_file_stat);
		file_size = input_file_stat.st_size;
		file_data = mmap(0, file_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, input_file_fd, 0);
		close(input_file_fd);
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

/*todo: optimize bounds checks */
t_pixel	*find_header_start(t_pixel *data, long long row, long long col, long long width, long long height) {

	printf("found a header pixel\n");

	long long	down_count = 0;
	while (row + down_count + 1 < height && down_count < HEADER_HEIGHT - 1 && possible_header_pixel(data[col + (row + 1 + down_count) * width])) {
		down_count++;
	}

	long long	up_count = 0;
	while (row - up_count - 1 >= 0 && up_count < HEADER_HEIGHT - 1) {
		if (!possible_header_pixel(data[col + (row - 1 - up_count) * width])) {
			printf("cant go up duo to no heade pixel\n");
			break ;
		}
		up_count++;
	}

	if (up_count + down_count < HEADER_HEIGHT - 1) {
		return (NULL);
	}

	row = row + down_count;

	printf("went up and down\n");
	FT_ASSERT(up_count + down_count == HEADER_HEIGHT - 1);

	long long	right_count = 0;
	while (right_count < HEADER_WIDTH - 1) {
		if (col + right_count + 1 >= width) {
			printf("break: reached width\n");
			break ;
		} else if (!possible_header_pixel(data[col + right_count + 1 + width * row])) {
			printf("break duo to no header\n");
			break ;
		}
		right_count++;
		printf("right count: %lld\n", right_count);
	}
	if (right_count < HEADER_WIDTH - 1) {
		return (NULL);
	}
	printf("went right\n");
	FT_ASSERT(right_count == HEADER_WIDTH - 1);
	return (data + col + width * row);
}

/* returns offset of src to the first header pixel,
 * -1 if there is no header pixel in the givne n bytes.
 * Assumes src to be 16 byte allinged data
*/
//static __attribute__((always_inline))
int	first_header_pixel(t_pixel *src) {
	FT_ASSERT(((uintptr_t)src) % 16 == 0);
	__m128i	mask = _mm_set1_epi32(0xffbcd900);//4th byte does not matter
	__m128i	data = _mm_load_si128((__m128i *)(src));
	__m128i	cmp8 = _mm_cmpeq_epi8(mask, data);
	int16_t	matches_bit_pattern = _mm_movemask_epi8(cmp8);
	int16_t	bit_mask = 0b111;
	int8_t	offset = 0;
	while (offset > (-16 / (int8_t)sizeof(t_pixel))) {
		if ((matches_bit_pattern & bit_mask) == bit_mask) {
			printf("return offset: %d\n", offset);
			return (offset);
		}
		offset--;
	}
	return (-1);
}

/*
todo: find smart way
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


t_pixel	*find_header(long long start_row, long long end_row, t_pixel *data, long long width) {
	FT_ASSERT(sizeof(t_pixel) == 4);
	long long	row = start_row + 7;
	for (; row <= end_row; row += HEADER_HEIGHT) {
		t_pixel	*cur_row = data + row * width;
		long long col =  0;
	
		/* alignment, inefficient but not significant */
		while (col < width && 0 != (((uintptr_t) (cur_row + col)) % SIMD_ALIGNMENT)) {
			if (possible_header_pixel(data[col + row * width])) {
				t_pixel	*header = find_header_start(data, row, col, width, end_row);
				if (header) {
					return (header);
				}
			}
			col++;
		}

		while (col < width) {
			if (col <= width - 16) {
				FT_ASSERT(((uintptr_t)(cur_row + col)) % 16 == 0);
				int	tmp = first_header_pixel(cur_row + col);
				if (tmp != -1) {
					t_pixel	*header = find_header_start(data, row, col, width, end_row);
					if (header) {
						return (header);
					}
				}
				col += 4;
			} else {
				if (possible_header_pixel(data[col + row * width])) {
					t_pixel	*header = find_header_start(data, row, col, width, end_row);
					if (header) {
						return (header);
					}
				}
				col++;
			}
		}
	}
	return (NULL);
}

typedef struct {
	t_pixel	*data;
	long long width;
	long long start_row;
	long long end_row;
}	t_thread_data;

void *find_header_thread(void *args) {
	t_thread_data *data = (t_thread_data *)args;
	data->data = find_header(data->start_row, data->end_row, data->data, data->width);
	return (0);
}

t_pixel	*find_header_threaded(t_pixel *data, long long height, long long width) {
	const u8		thread_count = 6;
	pthread_t		threads[thread_count];
	t_thread_data	thread_data[thread_count];
	long long		rows_per_thread = height / thread_count;

	for (u8 i = 0; i < thread_count; i++) {
		thread_data[i].width = width;
		thread_data[i].start_row = 7 + rows_per_thread * i;
		thread_data[i].end_row = thread_data[i].start_row + rows_per_thread;
		thread_data[i].data = data;
		if (thread_data[i].end_row > height) {
			thread_data[i].end_row = height;
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
	__builtin_unreachable();
	return (0);
}

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

void	print_msg_basic(struct bmp_header header, t_file file) {
	t_pixel	*data =  (t_pixel *) (file.data + header.data_offset);
	
	//data = find_header_threaded(data, header.height, header.width);
	data = find_header(0, header.height - 1, data, header.width);
	FT_ASSERT(data);
	uint16_t	len = ((uint16_t)data[7].r) + data[7].b;
	printf("len: %u\n", len);
	data = skip_header(data, header.width);
	size_t	alloc_size = (len + 32 + 15) & ~((size_t)15);
	char	*output = aligned_alloc(16, alloc_size);
	long long row = 0;
	size_t	out_i = 0;

	while (out_i < len) {
		long long col = 0;
		while (col < 6) {
			long long i = -row * header.width + col;
			if (len -out_i >= 6) {
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
				out_i += 2;
				goto ret;
			} else if (len - out_i == 1) {
				memcpy(output + out_i, data + i, 1);
				out_i += 1;
				goto ret;
			}
		}
		row++;
	}
ret:
	write(1, output, len);
	free(output);
}

int	main(int argc, char** argv) {
	if (argc != 2) {
		PRINT_ERROR("Usage: decode <input_filename>\n");
		return 1;
	}
	struct file_content file_content = read_entire_file(argv[1]);
	if (file_content.data == NULL) {
		PRINT_ERROR("Failed to read file\n");
		return 1;
	}
	struct bmp_header header = ((struct bmp_header*) file_content.data)[0];

	print_msg_basic(header, file_content);


	munmap(file_content.data, file_content.size);
	return 0;
}

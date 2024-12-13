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
#include <stdbool.h>


#ifndef NDEBUG
# define NDEBUG
#endif

//from libft
#include "timeval.c"
/* only files pasted in here will work of libft */
#include "libft.h"
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


typedef union u_pixel {
	u32		raw;
	struct {
		u8	b;
		u8	g;
		u8	r;
		u8	a;
	}__attribute__((packed));
	u8		arr[4];
}	__attribute__((packed)) t_pixel;

typedef struct {
	t_pixel	*pixels;
	size_t	width;
	size_t	height;
	void	*to_free;
}	t_data;

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



/* can be refactored but fine for now */
/* todo: for hure files don't read file in 1 go, read chunks and start threads
	on those chunks before reading more chunks */
static
t_data	read_entire_file(char* filename) {
	t_data				ret = {0};
	struct bmp_header	*header = NULL;

	int input_file_fd = open(filename, O_RDONLY);
	if (input_file_fd >= 0) {
		struct stat input_file_stat = {0};
		stat(filename, &input_file_stat);
		if (1) {
			size_t	alloc_size = (input_file_stat.st_size + 4 + 15) & ~((size_t)15);
			header = aligned_alloc(16, alloc_size);
			FT_ASSERT(header);
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
			/*leaks but it's only for debugging anyway */
			header =
				mmap(0, input_file_stat.st_size,
						PROT_READ | PROT_WRITE, MAP_PRIVATE, input_file_fd, 0);
		}
	}
	ret.to_free = header;
	ret.pixels = (t_pixel *)(((u8 *)ret.to_free) + header->data_offset);
	ret.width = header->width;
	ret.height = header->height;
	FT_ASSERT(errno == 0);
	return (ret);
}

static __attribute__((always_inline))
int	possible_header_pixel(t_pixel p) {
	if (p.b == 0x7f && p.g == 0xbc && p.r == 0xd9) {//this is barly used and has 5% run time
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
		/*todo: prefetch (14% runtime the line below) */
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
	if (__builtin_expect(col + 7 < width, 1)) {
		for (int8_t i = 1; i < 7; i++) {
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
 * Assumes little endian.
*/
static __attribute__((always_inline))
int8_t	first_header_pixel(t_pixel *src) {// function entry about 0-7.7 %runtime?
	FT_ASSERT(((uintptr_t)src) % 16 == 0);
	__m128i	data = _mm_load_si128((__m128i *)(src));//0.333-0.555 or mem access speed, about 8-12%runtime
	__m128i	care_bytes = _mm_set1_epi32(0x00ffffff);//non native, might be slow, about 3-8% runtime
	data = _mm_and_si128(data, care_bytes);//0.333, about 8-12% runtime
	__m128i	mask = _mm_set1_epi32(0x00d9bc7f);//non native, might be slow, about 0-3% runtime
	__m128i	cmp = _mm_cmpeq_epi32(data, mask);//0.5, 0-8% runtime
	int16_t	matches_bit_pattern = _mm_movemask_epi8(cmp);//1.0, about 8-17,6 % runtime

	//if (__builtin_expect(!matches_bit_pattern, true)) {
	if (!matches_bit_pattern) {//about 12-15% runtime
		return (-1); //between 0-15 % runtime ?
	}
	/* not significant rune time */
	uint8_t	trailing_zeros = __builtin_ctz(matches_bit_pattern);
	switch (trailing_zeros) {
		case (0): return (0);
		case (4): return (1);
		case (8): return (2);
		case (12): return (3);
		default: FT_ASSERT(0);
	}
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
	long long	next_end = end_row + HEADER_HEIGHT - (end_row % HEADER_HEIGHT);

	next_end = next_end >= height ? height - 1 : next_end;
	end_row = end_row % HEADER_HEIGHT ? next_end : end_row;
	for (; row <= end_row; row += 8) {
		FT_ASSERT(row >= 0);
		FT_ASSERT(row < height);
		t_pixel	*cur_row = data + row * width;
		long long col =  0;
		/* alignment, inefficient but not significant */
		while (col < width && 0 != (((uintptr_t) (cur_row + col)) % 16)) {
			if (__builtin_expect(possible_header_pixel(cur_row[col]), 0)) {
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
				//if (__builtin_expect(tmp != -1, 0)) {
				if (__builtin_expect(tmp != -1, 0)) {
					t_pixel	*header = find_header_start(data, row, col + tmp, width, height);
					if (__builtin_expect(header != NULL, 0)) {
						return (header);
					}
				}
				col += 4;
			} else {
				if (__builtin_expect(possible_header_pixel(cur_row[col]), 0)) {
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
	t_pixel		*pixels;
	long long	width;
	long long 	height;
	long long 	start_row;
	long long 	end_row;
}	t_thread_data;

void	*find_header_thread(void *args) {
	t_thread_data *data = (t_thread_data *)args;
	data->pixels = find_header(data->start_row, data->end_row, data->height, data->pixels, data->width);
	return (0);
}

t_pixel	*find_header_threaded(t_data data) {
	const u8		thread_count = 1;
	pthread_t		threads[thread_count];
	t_thread_data	thread_data[thread_count];
	long long		rows_per_thread = data.height / thread_count;
	long long		cur_start = 0;
	
#ifndef NDEBUG
	printf("height: %lld; width: %lld\n", height, width);
#endif
	for (u8 i = 0; i < thread_count; i++) {
		thread_data[i].width = data.width;
		thread_data[i].height = data.height;
		thread_data[i].start_row = cur_start;
		cur_start += rows_per_thread;
		thread_data[i].end_row = cur_start - 1;
		thread_data[i].pixels = data.pixels;
		if (i == thread_count - 1) {
			thread_data[i].end_row = data.height - 1;
		}
		pthread_create(threads + i, 0, find_header_thread,  thread_data + i);
	}
	for (size_t i = 0; i < thread_count; i++) {
		pthread_join(threads[i], 0);
		if (thread_data[i].pixels) {
			for (size_t j = i + 1; j < thread_count; j++) {
				pthread_join(threads[j], 0);
			}
			return (thread_data[i].pixels);/*todo: async thread waiting for this address to change */
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

void	print_msg_basic(t_data data) {
	
	data.pixels = find_header_threaded(data);
	//data = find_header(0, header.height - 1, data, header.width);
	if (!data.pixels) {
		return ;
	}
	uint16_t	len = ((uint16_t)data.pixels[7].r) + ((uint16_t)data.pixels[7].b);
	data.pixels = skip_header(data.pixels, data.width);
	/* write right back to the data buffer because why not, be carefull with
	 * memcpy since the memory region can overlap */
	char	*output = (i8 *)data.pixels;
	long long row = 0;
	size_t	out_i = 0;

	while (out_i < len) {
		long long col = 0;
		while (col < 6) {
			long long i = -row * data.width + col;
			if (len - out_i >= 6) {
				__m128i val= _mm_loadu_si64(data.pixels + i);
				_mm_storeu_si64(output + out_i, val);
				output[out_i + 3] = output[out_i + 4];
				output[out_i + 4] = output[out_i + 5];
				output[out_i + 5] = output[out_i + 6];
				out_i += 6;
				col += 2;
			} else if (len - out_i >= 3) {
				output[out_i] = data.pixels[i].arr[0];
				output[out_i + 1] = data.pixels[i].arr[1];
				output[out_i + 2] = data.pixels[i].arr[2];
				out_i += 3;
				col++;
			} else if (len - out_i == 2) {
				output[out_i] = data.pixels[i].arr[0];
				output[out_i + 1] = data.pixels[i].arr[1];
				goto ret;
			} else if (len - out_i == 1) {
				output[out_i] = data.pixels[i].arr[0];
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

#include <sys/wait.h>
static __attribute__((always_inline))
void	program(char *path) {
	//pid_t	pid = fork();

	//if (pid) {
	//	FT_ASSERT(pid > 0);
	//	int	stat;
	//	waitpid(pid, &stat, 0);
	//	return ;
	//}

	t_data data = read_entire_file(path);
	FT_ASSERT(data.to_free && data.pixels);

	print_msg_basic(data);
	free(data.to_free);

	//exit(0);
}

/*todo: use printable char range in bytes as hints where the header might be */

#include <sys/time.h>


#ifdef _PROFILER
#undef NDEBUG
void	set_profiling_interval(int usec) {
	struct itimerval timer;

	timer.it_value.tv_sec = 0;
	timer.it_value.tv_usec = usec;
	setitimer(ITIMER_PROF, &timer, NULL);
}

int	main(int argc, char** argv) {
	set_profiling_interval(10);
#else
int	main(int argc, char** argv) {
#endif //_PROFILER

	struct timeval	start;
	gettimeofday(&start, 0);
	errno = 0;
	for (size_t j = 0; j < 1; j++) {
		for (int i = 1; i < argc; i++) {
#ifndef NDEBUG
			printf("%s:\n", argv[i]);
			program(argv[i]);
			printf("\n");
#else
			program(argv[i]);
#endif //NDEBUG
		}
	}
	struct timeval	end;
	gettimeofday(&end, 0);
	struct timeval	diff = sub_timeval(end, start);
	/* 0.13 - 0.3 seconds on my laptop for all bmp file
	 * faster with a few seconds brakes after running the program;;13.12 */
	printf("\ntotal runtime: %lf seconds\n", diff.tv_sec + diff.tv_usec / 1000000.0);
	return (0);
}

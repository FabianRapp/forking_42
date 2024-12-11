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

typedef char i8;
typedef unsigned char u8;
typedef unsigned short u16;
typedef int i32;
typedef unsigned u32;
typedef unsigned long u64;

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
	}
	return (struct file_content){file_data, file_size};
}

int	possible_header_pixel(t_pixel p) {
	if (p.b == 127 && p.g == 188 && p.r == 217) {
		return (1);
	}
	return (0);
}

t_pixel	*find_header(long long start_row, long long end_row, t_pixel *data, long long width) {
	for (long long row = start_row; row < end_row; row++) {
		for (long long col = 0; col < width - 8; col++) {
			long long	guess_col = 0;
			/* check if header is horizontal*/
			while (guess_col < 7) {
				if (!possible_header_pixel(data[col + guess_col + width * row])) {
					break ;
				}
				guess_col++;
			}
			if (guess_col != 7) {
				continue ;
			} else {
			}

			/* check if header is to the left up */
			for (long long guess_row = 0; guess_row < 8; guess_row++) {
				if (!possible_header_pixel(data[col + width * (row - guess_row)])) {
					break ;
				}
				if (guess_row == 7) {
					return (data + row * width + col);
				}
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

void	*find_header_thread(void *args) {
	t_thread_data *data = (t_thread_data *)args;
	data->data = find_header(data->start_row, data->end_row, data->data, data->width);
	return (0);
}

t_pixel	*find_header_threaded(t_pixel *data, long long height, long long width) {
	const u8		thread_count = 8;
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
	
	data = find_header_threaded(data, header.height, header.width);
	uint16_t	len = ((uint16_t)data[7].r) + data[7].b;
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
				goto ret;
			} else if (len - out_i == 1) {
				memcpy(output + out_i, data + i, 1);
				goto ret;
			}
		}
		row++;
	}
ret:
	write(1, output, len);
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


	return 0;
}

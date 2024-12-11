#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include "main.h"
#include <errno.h>

#include <assert.h>

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
		close(input_file_fd);
	}
	return (struct file_content){file_data, file_size};
}

int	possible_header_pixel(t_pixel p) {
	if (p.b == 127 && p.g == 188 && p.r == 217) {
		return (1);
	}
	return (0);
}

t_pixel	*find_header(t_pixel *data, long long height, long long width) {
	for (long long row = 7; row < height; row++) {
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
				assert(row >= guess_row);
				if (!possible_header_pixel(data[col + width * (row - guess_row)])) {
					break ;
				}
				if (guess_row == 7) {
					//printf("row: %lu, col: %lu\n", row, col);
					//for (size_t i =0; i < width; i++) {
					//	data[row * width + col + i].r = 0xff;
					//	data[row * width + col + i].g = 0x0;
					//	data[row * width + col + i].b = 0x0;
					//}
					return (data + row * width + col);
				}
			}
		}
	}
	return (NULL);
}

t_pixel	*skip_header(t_pixel *header, size_t height, size_t width) {
	return (header - width - width + 2);
}

void	print_msg_basic(struct bmp_header header, t_file file) {
	void *base = file.data;
	t_pixel	*data =  (t_pixel *) (file.data + header.data_offset);
	
	data = find_header(data, header.height, header.width);
	uint16_t	len = ((uint16_t)data[7].r) + data[7].b;

	data = skip_header(data, header.height, header.width);
	char *chars = (char *)data;
	long long row = 0;
	while (1) {
		for (long long col = 0; col < 6; col++) {
			long long i = -row * header.width + col;
			chars = (char *)(data + i);
			if (len >= 3) {
				//write(1, data, 3);
				printf("%c%c%c", data[i].b, data[i].g, data[i].r);
				len -= 3;
			} else if (len == 2) {
				//write(1, data, 2);
				printf("%c%c", chars[0], chars[1]);
				return ;
			} else if (len == 1) {
				//write(1, data, 1);
				printf("%c", chars[0]);
				return ;
			} else {
				return ;
			}
		}
		row++;
	}
	//int fd = open("test.bmp", O_WRONLY | O_CREAT, 777);
	//write(fd, file.data, file.size);
	//printf("errno: %s\n", strerror(errno));
	//for (size_t col = 0; col < 6; col++) {
	//	printf("%c%c%c", data[col].b, data[col].g, data[col].r);
	//	//printf("%x", data[col].raw);
	//	data++;
	//}
	printf("\n");
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

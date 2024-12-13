#ifndef LIBFT_MACROS
# define LIBFT_MACROS

# ifndef CACHE_LINE_SIZE
#  define CACHE_LINE_SIZE 64
# endif // CAHCE_LINE_SIZE



# ifdef NDEBUG

#  ifndef FT_ASSERT
#   define FT_ASSERT(cond) \
	do { \
		if(!(cond)) {\
			__builtin_unreachable(); \
		}\
	} while(0)
#  endif //FT_ASSERT

#  else //NDEBUG
#   include <assert.h>
#	include <errno.h>
#	include <string.h>
#  ifndef FT_ASSERT
#   define FT_ASSERT(cond) \
	do { \
		if (!(cond)) {\
			if (errno) { \
				printf("%s\n", strerror(errno)); \
			} \
			assert(cond); \
		} \
	} while(0)
#  endif //FT_ASSERT

# endif //NDEBUG

#endif //LIBFT_MACROS

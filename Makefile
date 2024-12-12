CC := cc

NAME := decoder

INCLUDES := -I./includes
RNG := false
NDBUG := -DNDBUG
#-Werror
CFLAGS := -Wall -Wextra -Werror -g -O0 -fsanitize=undefined $(INCLUDES) -DRANDOM=$(RNG) \

#-Wno-shadow -Wshadow

FSAN := address

SRCS_DIR := srcs/
SRCS := main.c \

SRCS := $(SRCS:%=$(SRCS_DIR)%)
#SRCS := $(addprefix $(SRCS_DIR), $(SRCS))

OBJS_DIR := objs/
OBJS := $(SRCS:$(SRCS_DIR)%.c=$(OBJS_DIR)%.o)

GREEN	=	\033[0;32m
YELLOW	=	\033[33m
CYAN	=	\033[0;36m
CLEAR	=	\033[0m

.PHONY: all normal leaks fsan clean fclean re compile_commands


TASK_COMPILE = gcc -O0 -Wall -Wextra -Werror main.c -o decoder -lpthread


all: $(NAME)



$(NAME): main.c
	gcc -O0 -Wall -Wextra -Werror -g -fsanitize=undefined main.c -o decoder -lpthread
	#$(CC) $(CFLAGS) $(OBJS) -o $(NAME) -lpthread 
 
normal: $(NAME)
	@echo "$(GREEN) Compiled $(NAME) $(CLEAR)"

leaks: clean
	make CFLAGS="$(CXXFLAGS) -g -DLEAKS"
	@echo "$(GREEN) Compiled $(NAME) for 'system(\"leaks $(NAME)\");' $(CLEAR)"

fsan: clean
	make CFLAGS="$(CXXFLAGS) -g -fsanitize=$(FSAN) -DFSAN"
	@echo "$(GREEN) Compiled $(NAME) with '-fsanitize=$(FSAN)' $(CLEAR)"

$(OBJS_DIR)%.o: $(SRCS_DIR)%.c
	@mkdir -p $(@D)
	@$(CC) -c $< -o $@ $(CFLAGS) 

clean:
	@rm -f $(OBJS)
	@echo "$(YELLOW) Cleaned object files $(CLEAR)"

fclean:
	@rm -rf $(OBJS_DIR)
	@rm -f $(NAME)
	@echo "$(YELLOW) Cleaned object files, build directories and executables \
		$(CLEAR)"

re: fclean normal

PWD = $(shell pwd)
compile_commands:
	@echo '[' > compile_commands.json
	@$(foreach src, $(SRCS), \
		echo "\t{" >> compile_commands.json; \
		echo "\t\t\"directory\": \"$(PWD)\"," >> compile_commands.json; \
		echo "\t\t\"command\": \"$(CC) $(CFLAGS) -o $(OBJS_DIR)$$(basename $(src) .c).o $(src)\"," >> compile_commands.json; \
		echo "\t\t\"file\": \"$(src)\"" >> compile_commands.json; \
		echo "\t}," >> compile_commands.json;)
	@sed -i '' -e '$$ d' compile_commands.json
	@echo "\t}" >> compile_commands.json
	@echo ']' >> compile_commands.json
	@echo "$(YELLOW) Pseudo compile_commands.json generated $(CLEAR)"


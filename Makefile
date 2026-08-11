# ================================= NAME =================================== #

NAME = webserv

# =============================== DIRECTORIES =============================== #

SRCDIR		= srcs/
OBJDIR		= objs/
INCDIR		= includes/

# ================================= FILES =================================== #

SRC_FILES	= main.cpp


SRC = $(addprefix $(SRCDIR), $(SRC_FILES))
# ================================ OBJECTS =================================== #

OBJS = $(addprefix $(OBJDIR), $(SRC:$(SRCDIR)%.cpp=%.o))

# =============================== COMPILER ================================== #

CC				= c++
CFLAGS			= -Wall -Wextra -Werror -std=c++98 -g
INCLUDES		= -I$(INCDIR)
RM				= rm -f
MAKE			= make

# ================================= COLORS =================================== #

DEF_COLOR	= \033[0;39m
GRAY		= \033[0;90m
RED			= \033[0;91m
GREEN		= \033[0;92m
YELLOW		= \033[0;93m
BLUE		= \033[0;94m
LIGHT_BLUE	= \033[38;2;85;205;252m
PINK		= \033[38;2;247;168;184m
MAGENTA		= \033[0;95m
CYAN		= \033[0;96m
WHITE		= \033[0;97m

# ================================= RULES ==================================== #

all: $(NAME)

# --------------------------------- Targets ---------------------------------- #
$(NAME): $(OBJS)
	@echo "$(YELLOW)Linking $(NAME)...$(DEF_COLOR)"
	@$(CC) $(CFLAGS) $(OBJS) -o $(NAME)
	@echo "$(GREEN)✓ $(NAME) created successfully!$(DEF_COLOR)"

# ----------------------------- Object Files -------------------------------- #
$(OBJDIR)%.o: $(SRCDIR)%.cpp
	@mkdir -p $(dir $@)
	@echo "$(CYAN)Compiling $<...$(DEF_COLOR)"
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# ------------------------------ Clean Rules -------------------------------- #
clean:
	@echo "$(RED)Cleaning object files...$(DEF_COLOR)"
	@rm -rf $(OBJDIR)
	@echo "$(GREEN)✓ Object files cleaned!$(DEF_COLOR)"

fclean: clean
	@echo "$(RED)Cleaning executables...$(DEF_COLOR)"
	@$(RM) $(NAME)
	@echo "$(GREEN)✓ All files cleaned!$(DEF_COLOR)"

re: fclean all

reclear: 
	@make re && clear

r: reclear
	@./$(NAME)

v: reclear
	valgrind --leak-check=full  --track-origins=yes --show-leak-kinds=all --track-fds=yes ./$(NAME)

# ------------------------------- Phony Targets ----------------------------- #
.PHONY: all clean fclean re
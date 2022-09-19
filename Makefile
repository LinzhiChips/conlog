#
# Copyright (C) 2022 Linzhi Ltd.
#
# This work is licensed under the terms of the MIT License.
# A copy of the license can be found in the file COPYING.txt
#

NAME = conlog

CFLAGS = -g -Wall -Wextra -Wshadow -Wno-unused-parameter \
         -Wmissing-prototypes -Wmissing-declarations

OBJS = $(NAME).o

include Makefile.c-common


.PHONY:		all spotless

all::		| $(OBJDIR:%/=%)
all::		$(OBJDIR)$(NAME)

$(OBJDIR:%/=%):
		mkdir -p $@

$(OBJDIR)$(NAME): $(OBJS_IN_OBJDIR)

spotless::
		rm -f $(NAME)

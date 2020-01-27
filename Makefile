# Usage:
# make        # compile all binary
# make clean  # remove ALL binaries and objects

.SUFFIXES:

CC = musl-gcc

INCLUDES = -I./deps/jsmn -I ./deps/freebsd

LFLAGS = -static
CFLAGS = -fPIC -ffunction-sections -fdata-sections $(INCLUDES)

OBJDIR = build/_X86_64

SRCS := $(wildcard src/*.c)
OBJS := $(addprefix $(OBJDIR)/, $(addsuffix .o,$(basename $(SRCS))))
OBJS := $(subst src/,,$(OBJS))
BINS := chain_reactor

$(OBJDIR):
	mkdir -p $(OBJDIR)

finalize_chain_reactor:
	printf "%.8X" $(shell stat -c %s $(OBJDIR)/chain_reactor) | sed -E "s/(..)(..)(..)(..)/\4\3\2\1/" | xxd -r -g0 -p > $(OBJDIR)/chain_reactor.size
	objcopy $(OBJDIR)/chain_reactor --update-section .elf_size=$(OBJDIR)/chain_reactor.size $(OBJDIR)/chain_reactor

chain_reactor: $(OBJS)
	$(CC) $(LFLAGS) $(OBJS) -o $(OBJDIR)/$@
	strip -s $(OBJDIR)/chain_reactor
	$(MAKE) finalize_chain_reactor

$(OBJDIR)/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

all: $(OBJDIR) $(BINS)

clean:
	rm -rvf $(OBJDIR)

.DEFAULT_GOAL := all

.PHONY = all clean finalize_chain_reactor

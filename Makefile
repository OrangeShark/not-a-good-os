CC	= gcc
CFLAGS	= -Wall -m32 -I./include/ -nostdlib -fno-builtin -nostartfiles \
	  -nodefaultlibs -mno-sse -O3 -g
AS	= nasm
ASFLAGS = -f elf -Ox

BINTYPE = elf_i386
LDCFG	= linker.ld
LDFLAGS = -m$(BINTYPE) -T $(LDCFG)
LD	= ld

BINDIR	= bin
DEPSDIR = deps
MKDIRS	= $(CURDIR)/{$(BINDIR),$(DEPSDIR)}

TARGET	= $(BINDIR)/kernel.bin
SYMS	= $(BINDIR)/kernel.syms

VPATH	= boot drivers fs kernel lib mem
# boot
SRCS	+= kernel.c
ASSRCS	+= loader.s
# drivers
SRCS	+= keyboard.c screen.c timer.c
# fs
SRCS	+= fs.c
# kernel
SRCS	+= console.c desc_tables.c isr.c panic.c printk.c
ASSRCS	+= gdt.s interrupt.s
# lib
SRCS	+= string.c vsprintf.c
# mem
SRCS	+= heap.c paging.c
ASSRCS	+= process.s

OBJS	= $(addprefix $(BINDIR)/,${SRCS:.c=.o})
ASOBJS	= $(addprefix $(BINDIR)/,${ASSRCS:.s=.o})
DEPS	= $(addprefix $(DEPSDIR)/,${SRCS:.c=.d})

.SUFFIXES :
.SUFFIXES : .o .c .s

$(shell `mkdir -p $(MKDIRS)`)

all : $(TARGET)

$(TARGET) : $(OBJS) $(ASOBJS) $(LDCFG)
	$(LD) $(LDFLAGS) -o $(TARGET) $(OBJS) $(ASOBJS)
	objcopy --only-keep-debug $(TARGET) $(SYMS)
	objcopy --strip-debug $(TARGET)

$(BINDIR)/%.o : %.c
	$(CC) $(CFLAGS) -o $@ -c $<

$(BINDIR)/%.o : %.s
	$(AS) $(ASFLAGS) -o $@ $<

$(DEPSDIR)/%.d : %.c
	@$(CC) $(CFLAGS) -M $< > $@.$$$$;			\
	sed -e '1s#^#$(BINDIR)/#'				\
	-e 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@;	\
	rm -f $@.$$$$

TAGS :
	find . -regex ".*\.[cChH]\(pp\)?" -print | etags -

clean :
	-rm -r $(BINDIR) $(DEPSDIR)

mrproper : clean
	-rm TAGS

-include $(DEPS)

.PHONY : clean mrproper TAGS

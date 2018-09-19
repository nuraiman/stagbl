# This file is meant to be included from $(STAGBL_ARCH)/Makefile, after
# $(STAGBL_ARCH)/variables.mk

OBJDIR ?= obj
LIBDIR ?= lib
BINDIR ?= bin
TESTDIR ?= test

all : library

.PHONY : library

# function to prefix directory that contains most recently-parsed
# makefile (current) if that directory is not ./
thisdir = $(addprefix $(dir $(lastword $(MAKEFILE_LIST))),$(1))
incsubdirs = $(addsuffix /local.mk,$(call thisdir,$(1)))
srctoobj = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(filter-out $(OBJDIR)/%,$(1)))

# Rules
include $(STAGBL_DIR)/rules.mk

# Initialize set of targets and recursively include files for all targets
libstagbl-y.c :=
include $(SRCDIR)/local.mk

# Build libstagbl from sources here
libstagbl = $(LIBDIR)/libstagbl.$(AR_LIB_SUFFIX)
libstagbl : $(libstagbl)
$(libstagbl) : $(call srctoobj,$(libstagbl-y.c))

library : $(libstagbl)

$(OBJDIR)/%.o: $(OBJDIR)/%.c
	$(STAGBL_COMPILE.c) $< -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $$(@D)/.DIR
	$(STAGBL_COMPILE.c) $< -o $@

.PHONY: all clean print

clean:
	rm -rf $(OBJDIR) $(LIBDIR)

srcs.c := $(libstagbl-y.c)
srcs.o := $(call srctoobj,$(srcs.c))
srcs.d := $(srcs.o:%.o=%.d)
# Tell make that srcs.d are all up to date.  Without this, the include
# below has quadratic complexity
$(srcs.d) : ;

-include $(srcs.d)

# Demo applications for testing and convenience. Since these are mainly
# intended as standalone example of using the library, we use a phony target
# to clean and rebuild them with their own makefiles, clumsily moving the resulting
# binary.
demos : library $(BINDIR)/.DIR
	$(MAKE) -C $(STAGBL_DIR)/demos/2d clean
	$(MAKE) -C $(STAGBL_DIR)/demos/2d
	mv $(STAGBL_DIR)/demos/2d/stagbldemo2d $(BINDIR)
	$(MAKE) -C $(STAGBL_DIR)/demos/3d clean
	$(MAKE) -C $(STAGBL_DIR)/demos/3d
	mv $(STAGBL_DIR)/demos/3d/stagbldemo3d $(BINDIR)

.PHONY: demos

# Tests
test : demos $(TESTDIR)/.DIR
	cd $(TESTDIR)	&& STAGBL_DIR=$(STAGBL_DIR) STAGBL_ARCH=$(STAGBL_ARCH) $(STAGBL_DIR)/tests/runTests.py -w pth.conf

test_clean : $(TESTDIR)/.DIR
	cd $(TESTDIR)	&& STAGBL_DIR=$(STAGBL_DIR) STAGBL_ARCH=$(STAGBL_ARCH) $(STAGBL_DIR)/tests/runTests.py -w pth.conf -p

.PHONY: test test_clean

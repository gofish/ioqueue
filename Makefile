#!/usr/bin/make

# Copyright (c) 2015, Jeremy R. Fishman
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of the <organization> nor the
#       names of its contributors may be used to endorse or promote products
#       derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


### Main build target
#
.PHONY: all
all:    targets

### Output verbosity
#
# Define V=... (to anything non-empty) to enable logging every action
#
$(V).SILENT:

ifeq ($(MAKELEVEL),0)
###
## Top-level Make
###

### Default build type if unspecified
#
ifeq ($(origin BUILD_TYPE),undefined)
ifeq ($(DEBUG),)
BUILD_TYPE := release
else
BUILD_TYPE := debug
endif
endif

### Export search path for source files
#
# Override this to change the source root directory
export VPATH ?= $(CURDIR)/

### Additional build types and overrides
#
-include Overrides.mk

### Run the clean rule directly
#
.PHONY: clean
clean:
	$(RM) -r build

### Force execution of all rules
#
.PHONY: force
%::     force

### Prevent implicit rules from remaking the Makefiles
#
MAKEFILE       := $(firstword $(MAKEFILE_LIST))
$(MAKEFILE):    ;
Overrides.mk:   ;

### Delegate release goals to a sub-make and override BUILD_TYPE
#
release debug profile coverage:: force
	# Goal '$@' overrides current build type '$(BUILD_TYPE)'
	$(eval export BUILD_TYPE=$@)
	# Execute this Makefile from a build-specific subdirectory
	DEST=$${DEST:-"build/$(BUILD_TYPE)"}; \
	    echo Building to $$DEST && \
	    mkdir -p "$$DEST" && \
	    $(MAKE) -C "$$DEST" -I $(CURDIR) -I $(VPATH) -f $(abspath $(MAKEFILE)) --no-print-directory $@
.PHONY: release debug profile coverage

### Delegate all unspecified goals to a sub-make
#
%:: force
	$(eval export BUILD_TYPE)
	# Execute this Makefile from a build-specific subdirectory
	DEST=$${DEST:-"build/$(BUILD_TYPE)"}; \
	    echo Building to $$DEST && \
	    mkdir -p "$$DEST" && \
	    $(MAKE) -C "$$DEST" -I $(CURDIR) -I $(VPATH) -f $(abspath $(MAKEFILE)) --no-print-directory $@

else
###
## Sub-level Make
###
release debug profile coverage: all

### Build-specific compile flags
#
CFLAGS_release  := -g -Wall -Werror -O1
CXXFLAGS_release:= -g -Wall -Werror -O1
LDFLAGS_release := -g
CFLAGS_debug    := -g -Wall -Werror
CXXFLAGS_debug  := -g -Wall -Werror
LDFLAGS_debug   := -g
CFLAGS_profile  := -pg $(CFLAGS_release)
CXXFLAGS_profile:= -pg $(CFLAGS_release)
LDFLAGS_profile := -pg $(CFLAGS_release)
CFLAGS_coverage := --coverage $(CFLAGS_debug)
CXXFLAGS_coverage:= --coverage $(CFLAGS_debug)
LDFLAGS_coverage:= --coverage $(CFLAGS_debug)
ifeq ($(BUILD_TYPE),coverage)
export CCACHE_DISABLE := "true"
endif

### Standard file extensions
AR_EXT  := .a
BIN_EXT :=
C_EXT   := .c
CC_EXT  := .cc
CPP_EXT := .cpp
DEP_EXT := .d
OBJ_EXT := .o
LIB_EXT := .so

### Non-standard tools (defaults)
#
GZIP    := gzip
TAR     := tar
PROTOC  := protoc
THRIFT  := thrift
GCOV    := gcov
LCOV    := lcov
GENHTML := genhtml

### Build and release configuration
#
-include Config.mk
CFLAGS  := $(CFLAGS_$(BUILD_TYPE)) $(CFLAGS)
CXXFLAGS:= $(CXXFLAGS_$(BUILD_TYPE)) $(CXXFLAGS)
LDFLAGS := $(LDFLAGS_$(BUILD_TYPE)) $(LDFLAGS)
LDLIBS  := $(LDLIBS) $(LDLIBS_$(BUILD_TYPE))

### Useful procedures
#

# Variable and Indentation helpers
#   - a variable containing a comma
#   - an empty variable
#   - a variable containing a space
#   - an indentation variable for pretty printing
#   - a procedure to announce a build step with relative paths
#
# See http://www.gnu.org/software/make/manual/make.html#Syntax-of-Functions
comma := ,
empty :=
space := $(empty) $(empty)
ifeq ($(origin indent),undefined)
indent := $(space)$(space)
endif
define announce
$(info $(indent)$(tab)$(subst $(VPATH),,$1))
endef
define announce_raw
$(info $(indent)$(tab)$(1))
endef

# A procedure for declaring dependencies
#
# Usage: $(call depends,foo,foo.o)
# Usage: $(call depends,bar bar.a,bar.o foo.o)
#
define depends
$(addprefix $(SUBDIR),$1): $(shell realpath -m --relative-base=. $(addprefix $(SUBDIR),$2))
endef
define depends_ext
$(addprefix $(SUBDIR),$1): $2
endef

# A procedure for declaring relative build flags
#
# Usage: $(call flags,path/to/foo/,CFLAGS,-g)
#
define flags
$(eval $2_$(patsubst ./%,%,$(SUBDIR))$1 += $3)
endef

# A procedure for installing source files to build locations
#
define install
$(eval ifeq ($1,$(dir $1))
$(foreach src,$2,$(eval $1$(src): $(SUBDIR)$(src)))
$1%: $(SUBDIR)% | $1.exists
	$$(call announce,IN  $$@)
	cp -a $$< $$@
else
$$(error Missing trailing slash on install: $1)
endif
)
endef

# A procedure to declare a test target
#
define test
$(foreach target,$1,$(eval test: test($(addprefix $(SUBDIR),$(target)))))
endef

# Procedures for backing up variables during makefile inclusions
#
empty :=
define save
SAVED_$1 := $($1)
$1 :=
endef
define save_from_var
SAVED_$1 := $($1)
$1 := $($1_$(SUBDIR))
endef
define restore
$1 := $(SAVED_$1)
endef
define restore_prefixed
$1 := $(SAVED_$1) $(addprefix $(SUBDIR),$($1))
endef
define restore_as_var
$1_$(SUBDIR) := $($1)
$1 := $(SAVED_$1)
endef

# A procedure for including Makefiles
#
# Usage: $(call include_rules,path/to/Rules.mk)
#
#   Define the variable $D to be the directory component of the
#   absolute or relative path to the Makefile, saving any previous
#   value for $D before inclusion, and restoring this value after.
#
define include_rules
# save variables
$(eval $(call save,SUBDIR))
$(eval SUBDIR := $(dir $1))
$(eval $(call save,SRCS))
$(eval $(call save,TGTS))
$(eval $(call save_from_var,CFLAGS))
$(eval $(call save_from_var,CXXFLAGS))
$(eval $(call save_from_var,LDFLAGS))
$(eval $(call save_from_var,LDLIBS))
# include subdirectory rules
$(eval $(call announce,MK  $1))
$(eval include $1)
# process and restore variable
$(eval $(call restore_prefixed,SRCS))
$(eval $(call restore_prefixed,TGTS))
$(eval $(call restore_as_var,CFLAGS))
$(eval $(call restore_as_var,CXXFLAGS))
$(eval $(call restore_as_var,LDFLAGS))
$(eval $(call restore_as_var,LDLIBS))
$(eval $(call restore,SUBDIR))
endef

### Project build rules
#

# Find all Rules.mk files under the source directory in breadth-first order
RULES := $(shell cd $(VPATH) && \
                find -L . -name Rules.mk -printf '%d\t%P\n' 2>/dev/null | \
                sort -n | cut -f2-)

# Include the subdirectory rules
$(foreach path,$(RULES),$(eval $(call include_rules,$(path))))

# Force build of targets, and do nothing if there are none
.PHONY:  force
targets: force $(TGTS)

### Dependency generation
#
# Support for C object files
SRCS_C  := $(filter %$(C_EXT),$(SRCS))
OBJS_C  := $(SRCS_C:$(C_EXT)=$(OBJ_EXT))
DEPS_C  := $(SRCS_C:$(C_EXT)=$(DEP_EXT))
# Support for C++ object files (.cc extension)
SRCS_CC := $(filter %$(CC_EXT),$(SRCS))
OBJS_CC := $(SRCS_CC:$(CC_EXT)=$(OBJ_EXT))
DEPS_CC := $(SRCS_CC:$(CC_EXT)=$(DEP_EXT))
# Support for C++ object files (.cpp extension)
SRCS_CPP:= $(filter %$(CPP_EXT),$(SRCS))
OBJS_CPP:= $(SRCS_CPP:$(CPP_EXT)=$(OBJ_EXT))
DEPS_CPP:= $(SRCS_CPP:$(CPP_EXT)=$(DEP_EXT))
-include $(DEPS_C) $(DEPS_CC) $(DEPS_CPP)

### Preprocessing pattern rules
#
# Subdirectory creation
DIRS := $(sort $(foreach file,$(SRCS) $(TGTS),$(dir $(file))))
$(SRCS) $(TGTS): | $(addsuffix .exists,$(DIRS))
%/.exists:
	mkdir -p $(dir $@)
	touch $@
.exists:

# C/C++ compile flag detection
#   Have the compiler emit a verbose assembly header for an empty file.
#   This header includes all supplied and implied flags, along with a
#   compiler checksum.  So long as a .o file depends on the .x, this
#   rule will be executed with the current flags, including any target-
#   specific modifications.  If the flags have changed the .x file is
#   updated, forcing the .o file to be rebuilt.

FLGS_C := $(sort $(foreach src,$(SRCS_C),$(dir $(src)).cflags))
$(FLGS_C): force
	new_flags=`$(CC) $(CFLAGS) $(CFLAGS_$(dir $@)) -S -fverbose-asm -o - -x c /dev/null 2>/dev/null`; \
	old_flags=`cat '$@' 2>/dev/null`; \
	    if [ x"$$new_flags" != x"$$old_flags" ]; then \
	        echo -n "$$new_flags" >'$@' || exit 1; \
	    fi

FLGS_CXX := $(sort $(foreach src,$(SRCS_CC) $(SRCS_CPP),$(dir $(src)).cxxflags))
$(FLGS_CXX): force
	new_flags=`$(CXX) $(CXXFLAGS) $(CXXFLAGS_$(dir $@)) -S -fverbose-asm -o - -x c++ /dev/null 2>/dev/null`; \
	old_flags=`cat '$@' 2>/dev/null`; \
	    if [ x"$$new_flags" != x"$$old_flags" ]; then \
	        echo -n "$$new_flags" >'$@' || exit 1; \
	    fi

# Protocol buffer C++ source generation
#   This implicit rule will combine with the DEPS .o rule in two steps
#   in order to generate %.pb.o from %.pb.cc from %.proto
%.pb.cc %.pb.h: %.proto
	$(call announce,PB  $<)
	$(PROTOC) -I$(dir $<) --cpp_out=$(dir $@) $<
	sed -i '1i #pragma GCC diagnostic ignored "-Wshadow"' $(@:.cc=.h) $(@:.h=.cc)
	sed -i '$a #pragma GCC diagnostic warning "-Wshadow"' $(@:.cc=.h) $(@:.h=.cc)

# Thrift protocol C++ source generation
#   This implicit rule will combine with the DEPS .o rule in two steps
#   in order to generate %.pb.o from %.pb.cc from %.thrift
%.cpp %.h: %.thrift
	$(call announce,TFT $<)
	$(THRIFT) -out $(dir $@) -gen cpp $<

### Compilation pattern rules
#
$(OBJS_C): %$(OBJ_EXT): %$(C_EXT)
	$(call announce,C   $<)
	$(CC) $(CFLAGS) $(CFLAGS_$(dir $@)) -c $< -MMD -MP -MT $@ -MF $(@:$(OBJ_EXT)=$(DEP_EXT)) -o $@
$(OBJS_CC): %$(OBJ_EXT): %$(CC_EXT)
	$(call announce,C++ $<)
	$(CXX) $(CXXFLAGS) $(CXXFLAGS_$(dir $@)) -c $< -MMD -MP -MT $@ -MF $(@:$(OBJ_EXT)=$(DEP_EXT)) -o $@
$(OBJS_CPP): %$(OBJ_EXT): %$(CPP_EXT)
	$(call announce,C++ $<)
	$(CXX) $(CXXFLAGS) $(CXXFLAGS_$(dir $@)) -c $< -MMD -MP -MT $@ -MF $(@:$(OBJ_EXT)=$(DEP_EXT)) -o $@
%$(OBJ_EXT): %$(C_EXT)
	$(error Need $@ but $< is not listed as a source file)
%$(OBJ_EXT): %$(CC_EXT)
	$(error Need $@ but $< is not listed as a source file)
%$(OBJ_EXT): %$(CPP_EXT)
	$(error Need $@ but $< is not listed as a source file)

### Linking pattern rules
#
%$(BIN_EXT): %$(OBJ_EXT)
	$(call announce,BIN $@)
	$(CXX) $(LDFLAGS) $(LDFLAGS_$(dir $@)) -o $@ $(filter %$(OBJ_EXT),$^) $(filter %$(AR_EXT),$^) $(filter %$(LIB_EXT),$^) $(LDLIBS_$(dir $@)) $(LDLIBS)
%$(LIB_EXT):
	$(call announce,LIB $@)
	$(CC) -shared $(LDFLAGS) $(LDFLAGS_$(dir $@)) -o $@ $^ $(LDLIBS_$(dir $@)) $(LDLIBS)
%$(AR_EXT):
	$(call announce,AR  $@)
	$(AR) rcs $@ $?
.PRECIOUS: %.tar
%.tar:
	$(call announce,TAR $@)
	$(TAR) rf $@ $?
%.tgz:
	$(call announce,TGZ $@)
	$(TAR) czf $@ $^
%.gz: %
	$(call announce,GZ  $@)
	$(GZIP) -c $< > $@
%.tgz: %.tar
	$(call announce,GZ  $@)
	$(GZIP) -c $< > $@
%.gcov:
	$(call announce,COV $(@:.gcov=))
	$(GCOV) -i $(subst $(VPATH),,$(@:.gcov=)) 2>/dev/null | \
                head -2 | tail -1 | sed 's/.*:/$(indent)$(indent)/'

test(%): %
	$(call announce,TST $(%))
	$%

coverage: test $(SRCS:=.gcov)
	$(call announce_raw,COV file://$(abspath index.html))
	$(LCOV) -c -b $(VPATH) -d . -o lcov.out --no-external > lcov.log
	$(GENHTML) lcov.out >genhtml.log

### Secondary expansion of certain prerequisites
#
.SECONDEXPANSION:
# Compilation directory flags
$(OBJS_C):    $$(dir $$@).cflags
$(OBJS_CC):   $$(dir $$@).cxxflags
$(OBJS_CPP):  $$(dir $$@).cxxflags

endif

#!/usr/bin/make

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

### Run the clean rule directly
#
clean:
	$(RM) -r build

### Additional build types and overrides
#
-include Overrides.mk

### Force execution of all rules
#
%::     force
.PHONY: force

### Prevent implicit rules from remaking the Makefiles
#
MAKEFILE       := $(firstword $(MAKEFILE_LIST))
$(MAKEFILE): ;
Overrides.mk: ;

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
CFLAGS_release  := -g -O1
CXXFLAGS_release:= -g -O1
LDFLAGS_release := -g
CFLAGS_debug    := -g
CXXFLAGS_debug  := -g
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

# A procedure to reverse a list
#
define reverse
$(if $(1),$(call reverse,$(wordlist 2,$(words $(1)),$(1)))) $(firstword $(1))
endef

# A procedure for declaring dependencies
#
# Usage: $(call depends,foo,foo.o)
# Usage: $(call depends,bar bar.a,bar.o foo.o)
#
define depends
$(addprefix $(SUBDIR),$1): $(addprefix $(SUBDIR),$2)
endef
define depends_ext
$(addprefix $(SUBDIR),$1): $2
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
$(eval $(call save,SRCS))
$(eval $(call save,TGTS))
$(eval $(call save,CFLAGS))
$(eval $(call save,CXXFLAGS))
$(eval $(call save,LDFLAGS))
$(eval $(call save,LDLIBS))
# include subdirectory rules
$(eval SUBDIR := $(dir $1))
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

# Find all Rules.mk files under the source directory in depth-last order
RULES := $(shell cd $(VPATH) && find . -depth \! -path '* *' -a -name Rules.mk 2>/dev/null)
RULES := $(patsubst ./%,%,$(RULES))
RULES := $(call reverse,$(RULES))

# Include the subdirectory rules
$(foreach path,$(RULES),$(eval $(call include_rules,$(path))))

# Force build of targets, and do nothing if there are none
.PHONY:  force
targets: force $(TGTS)

### Dependency generation
#
# Support for C object files
SRCS_C    := $(filter %$(C_EXT),$(SRCS))
DEPS_C_O  := $(SRCS_C:$(C_EXT)=$(OBJ_EXT))
DEPS_C_D  := $(SRCS_C:$(C_EXT)=$(DEP_EXT))
DEPS_C_F  := $(sort $(foreach src,$(SRCS_C),$(dir $(src)).cflags))
# Support for C++ object files (.cc extension)
SRCS_CC   := $(filter %$(CC_EXT),$(SRCS))
DEPS_CC_O := $(SRCS_CC:$(CC_EXT)=$(OBJ_EXT))
DEPS_CC_D := $(SRCS_CC:$(CC_EXT)=$(DEP_EXT))
# Support for C++ object files (.cpp extension)
SRCS_CPP  := $(filter %$(CPP_EXT),$(SRCS))
DEPS_CPP_O := $(SRCS_CPP:$(CPP_EXT)=$(OBJ_EXT))
DEPS_CPP_D := $(SRCS_CPP:$(CPP_EXT)=$(DEP_EXT))
DEPS_CC_F := $(sort $(foreach src,$(SRCS_CC) $(SRCS_CPP),$(dir $(src)).cxxflags))
-include $(DEPS_C_D) $(DEPS_CC_D) $(DEPS_CPP_D)

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
$(DEPS_C_F): force
	new_flags=`$(CC) $(CFLAGS) $(CFLAGS_$(dir $@)) -S -fverbose-asm -o - -x c /dev/null 2>/dev/null`; \
	old_flags=`cat '$@' 2>/dev/null`; \
	    if [ x"$$new_flags" != x"$$old_flags" ]; then \
	        echo -n "$$new_flags" >'$@' || exit 1; \
	    fi
$(DEPS_CC_F): force
	new_flags=`$(CXX) $(CXXFLAGS) $(CXXFLAGS_$(dir $@)) -S -fverbose-asm -o - -x c++ /dev/null 2>/dev/null`; \
	old_flags=`cat '$@' 2>/dev/null`; \
	    if [ x"$$new_flags" != x"$$old_flags" ]; then \
	        echo -n "$$new_flags" >'$@' || exit 1; \
	    fi
$(DEPS_CPP_F): force
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
$(DEPS_C_O): $(DEPS_C_F)
$(DEPS_C_O): %$(OBJ_EXT): %$(C_EXT)
	$(call announce,C   $<)
	$(CC) $(CFLAGS) $(CFLAGS_$(dir $@)) -c $< -MMD -MP -MT $@ -MF $(@:$(OBJ_EXT)=$(DEP_EXT)) -o $@
$(DEPS_CC_O): $(DEPS_CC_F)
$(DEPS_CC_O): %$(OBJ_EXT): %$(CC_EXT)
	$(call announce,C++ $<)
	$(CXX) $(CXXFLAGS) $(CXXFLAGS_$(dir $@)) -c $< -MMD -MP -MT $@ -MF $(@:$(OBJ_EXT)=$(DEP_EXT)) -o $@
$(DEPS_CPP_O): $(DEPS_CPP_F)
$(DEPS_CPP_O): %$(OBJ_EXT): %$(CPP_EXT)
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
	$(LCOV) -c -b $(VPATH) -d . -o lcov.out --no-external > lcov.log
	$(GENHTML) lcov.out >genhtml.log

endif

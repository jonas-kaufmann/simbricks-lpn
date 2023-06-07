# Copyright 2021 Max Planck Institute for Software Systems, and
# National University of Singapore
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
# CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

include mk/subdir_pre.mk

dir_gcd := $(d)
bin_gcd := $(d)gcd_verilator
verilator_obj_dir := $(d)obj_dir
verilator_src_gcd := $(verilator_obj_dir)/Vgcd.cpp
verilator_bin_gcd := $(verilator_obj_dir)/Vgcd

srcs_gcd := $(addprefix $(d),gcd_verilator.cc)

bin_gcd_workload := $(d)gcd_workload
OBJS := $(addprefix $(d),gcd_workload.o vfio.o)


$(verilator_src_gcd):
	$(VERILATOR) $(VFLAGS) --cc -O3 --trace --trace-depth 1 \
	    -CFLAGS "-I$(abspath $(lib_dir)) -iquote $(abspath $(base_dir)) -Og -g -ggdb -Wall -Wno-maybe-uninitialized" \
	    --Mdir $(verilator_obj_dir) \
		-LDFLAGS "-L$(abspath $(lib_dir)) -lsimbricks" \
	    $(dir_gcd)gcd.v --exe $(abspath $(srcs_gcd))

$(verilator_bin_gcd): $(verilator_src_gcd) $(lib_simbricks) $(srcs_gcd)
	$(MAKE) -C $(verilator_obj_dir) -f Vgcd.mk

$(bin_gcd): $(verilator_bin_gcd)
	cp $< $@


# statically linked binary that can run under any Linux image
$(bin_gcd_workload): CPPFLAGS += -static
$(bin_gcd_workload): LDFLAGS += -static
$(bin_gcd_workload): $(OBJS)

CLEAN := $(bin_gcd) $(verilator_obj_dir) $(bin_gcd_workload) $(OBJS)
ALL := $(bin_gcd_workload)

ifeq ($(ENABLE_VERILATOR),y)
ALL += $(bin_gcd)
endif

include mk/subdir_post.mk

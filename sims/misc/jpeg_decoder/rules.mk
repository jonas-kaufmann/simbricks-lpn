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

verilator_obj_dir := $(d)obj_dir

# jpeg_decoder_verilator
# This simulates just the RTL of the JPEG decoder itself.
bin_jpeg_decoder := $(d)jpeg_decoder_verilator
verilator_bin_jpeg_decoder := $(verilator_obj_dir)/Vjpeg_decoder
verilator_mk_jpeg_decoder := $(verilator_bin_jpeg_decoder).mk
srcs_jpeg_decoder := $(addprefix $(d),jpeg_decoder_verilator.cc)
jpeg_decoder_top := $(d)rtl/src_v/jpeg_decoder.v
jpeg_decoder_search_paths := $(addprefix $(d),rtl/src_v rtl/jpeg_core/src_v)

$(verilator_mk_jpeg_decoder): $(lib_simbricks)
	$(VERILATOR) $(VFLAGS) --cc -O3 \
		--trace --no-trace-top --no-trace-params --trace-underscore \
		-CFLAGS "-I$(abspath $(lib_dir)) -iquote $(abspath $(base_dir)) -fsanitize=address -Og -g" \
		-LDFLAGS "-fsanitize=address -static-libasan" \
		--Mdir $(verilator_obj_dir) \
		--exe \
		$(addprefix -y ,$(jpeg_decoder_search_paths)) \
		$(jpeg_decoder_top) \
		$(abspath $(lib_simbricks)) \
		$(abspath $(srcs_jpeg_decoder))

$(verilator_bin_jpeg_decoder): $(verilator_mk_jpeg_decoder)  \
		 $(srcs_jpeg_decoder)
	$(MAKE) -C $(verilator_obj_dir) -f $(abspath $(verilator_mk_jpeg_decoder))

$(bin_jpeg_decoder): $(verilator_bin_jpeg_decoder)
	cp $< $@

# jpeg_decoder_multiple_verilator
# This simulates the whole RTL of everything that is pushed onto the FPGA in the
# AC/DSim project. For example, there are now AXI adapters added around the JPEG
# decoder. It uses the RTL directly expoprted from Vivado.
bin_jpeg_decoder_multiple2 := $(d)jpeg_decoder_multiple2_verilator
verilator_bin_jpeg_decoder_multiple2 := $(verilator_obj_dir)/Vjpeg_decoder_multiple2
verilator_mk_jpeg_decoder_multiple2 := $(verilator_bin_jpeg_decoder_multiple2).mk
srcs_jpeg_decoder_multiple2 := $(addprefix $(d),jpeg_decoder_multiple2_verilator.cc)

$(verilator_mk_jpeg_decoder_multiple2):
	$(VERILATOR) $(VFLAGS) --cc -O3 \
		--trace --no-trace-top --no-trace-params --trace-underscore \
		-CFLAGS "-I$(abspath $(lib_dir)) -iquote $(abspath $(base_dir))" \
		--Mdir $(verilator_obj_dir) \
		-LDFLAGS "-L$(abspath $(lib_dir)) -lsimbricks" \
		-y /home/jonask/Repos/jpeg_decoder_multiple2_sim/xsim/srcs/rtl/xil_defaultlib \
		-y /home/jonask/Repos/jpeg_decoder_multiple2_sim/xsim/srcs/sources_1/new \
		--exe \
		--top-module jpeg_decoder_multiple2 \
		/home/jonask/Repos/jpeg_decoder_multiple2_sim/xsim/srcs/sources_1/bd/jpeg_decoder_multiple2/ip/*/sim/* \
		/home/jonask/Repos/jpeg_decoder_multiple2_sim/xsim/srcs/sources_1/bd/jpeg_decoder_multiple2/sim/jpeg_decoder_multiple2.v \
		$(abspath $(srcs_jpeg_decoder_multiple2))

$(verilator_bin_jpeg_decoder_multiple2): $(verilator_mk_jpeg_decoder_multiple2) $(lib_simbricks) $(srcs_jpeg_decoder_multiple2)
	$(MAKE) -C $(verilator_obj_dir) -f $(abspath $(verilator_mk_jpeg_decoder_multiple2))

$(bin_jpeg_decoder_multiple2): $(verilator_bin_jpeg_decoder_multiple2)
	cp $< $@

# jpeg_decoder_workload
bin_workload := $(d)jpeg_decoder_workload
OBJS := $(bin_workload).o $(d)vfio.o

# statically linked binary that can run under any Linux image
$(bin_workload): CPPFLAGS += -static
$(bin_workload): LDFLAGS += -static
$(bin_workload): $(bin_workload).o $(d)vfio.o

CLEAN := $(bin_jpeg_decoder) $(verilator_obj_dir) $(bin_workload) $(OBJS)
ALL := $(bin_workload)

ifeq ($(ENABLE_VERILATOR),y)
ALL += $(bin_jpeg_decoder) $(bin_jpeg_decoder_multiple)
endif

include mk/subdir_post.mk

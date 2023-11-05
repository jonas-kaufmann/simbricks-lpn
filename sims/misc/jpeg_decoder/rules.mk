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
bin_jpeg_decoder := $(d)jpeg_decoder_verilator
verilator_bin_jpeg_decoder := $(verilator_obj_dir)/Vjpeg_decoder_dma
verilator_mk_jpeg_decoder := $(verilator_bin_jpeg_decoder).mk
srcs_jpeg_decoder := $(addprefix $(d),jpeg_decoder_verilator.cc axi.cc)

$(verilator_mk_jpeg_decoder):
	$(VERILATOR) $(VFLAGS) --cc -O3 \
		--trace --no-trace-top --no-trace-params --trace-underscore \
		-CFLAGS "-I$(abspath $(lib_dir)) -iquote $(abspath $(base_dir))" \
		--Mdir $(verilator_obj_dir) \
		-LDFLAGS "-L$(abspath $(lib_dir)) -lsimbricks" \
		-y /home/jonask/Repos/jpeg_decoder_sim/xsim/srcs/rtl/xil_defaultlib \
		-y /home/jonask/Repos/jpeg_decoder_sim/xsim/srcs/sources_1/new \
		--exe \
		--top-module jpeg_decoder_dma \
		/home/jonask/Repos/jpeg_decoder_sim/xsim/srcs/sources_1/bd/jpeg_decoder_dma/ip/*/sim/* \
		/home/jonask/Repos/jpeg_decoder_sim/xsim/srcs/sources_1/bd/jpeg_decoder_dma/sim/jpeg_decoder_dma.v \
		$(abspath $(srcs_jpeg_decoder))

$(verilator_bin_jpeg_decoder): $(verilator_mk_jpeg_decoder) $(lib_simbricks) $(srcs_jpeg_decoder)
	$(MAKE) -C $(verilator_obj_dir) -f $(abspath $(verilator_mk_jpeg_decoder))

$(bin_jpeg_decoder): $(verilator_bin_jpeg_decoder)
	cp $< $@

# jpeg_deocder_multiple_verilator
bin_jpeg_decoder_multiple2 := $(d)jpeg_decoder_multiple2_verilator
verilator_bin_jpeg_decoder_multiple2 := $(verilator_obj_dir)/Vjpeg_decoder_multiple2
verilator_mk_jpeg_decoder_multiple2 := $(verilator_bin_jpeg_decoder_multiple2).mk
srcs_jpeg_decoder_multiple2 := $(addprefix $(d),jpeg_decoder_multiple2_verilator.cc axi.cc)

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

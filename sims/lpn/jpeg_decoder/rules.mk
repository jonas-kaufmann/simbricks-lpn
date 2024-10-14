# Copyright 2024 Max Planck Institute for Software Systems, and
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

# JPEG decoder behavioral model
bin_jpeg_decoder_bm := $(d)jpeg_decoder_bm

bm_objs := $(addprefix $(d),jpeg_decoder_bm.o)
bm_objs += $(addprefix $(d), src/func_sim.o)
bm_objs += $(addprefix $(d), src/lpn_req_map.o)
bm_objs += $(addprefix $(d), lpn_def/places.o)

$(bin_jpeg_decoder_bm): CPPFLAGS += -O3 -g

# $(bin_jpeg_decoder_bm): CPPFLAGS += -fsanitize=address -g
# $(bin_jpeg_decoder_bm): LDFLAGS += -fsanitize=address -static-libasan
$(bin_jpeg_decoder_bm): $(bm_objs) $(lib_pciebm) $(lib_pcie) $(lib_base) \
	$(lib_lpnsim)  -lpthread

# workload driver
bin_workload_driver := $(d)jpeg_decoder_workload_driver
workload_driver_objs := $(bin_workload_driver).o $(d)vfio.o

# statically linked binary that can run under any Linux image
$(workload_driver_objs): CPPFLAGS += -static -g 
$(bin_workload_driver): LDFLAGS += -static -g

$(bin_workload_driver): $(workload_driver_objs)

CLEAN := $(bin_jpeg_decoder_bm) $(bin_workload_driver) $(bm_objs) \
	$(workload_driver_objs)
ALL := $(bin_jpeg_decoder_bm) $(bin_workload_driver)

include mk/subdir_post.mk
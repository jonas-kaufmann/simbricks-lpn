/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*!
 * \file vta/driver.h
 * \brief Driver interface that is used by runtime.
 *
 * Driver's implementation is device specific.
 */

#ifndef VTA_IOGEN_H_
#define VTA_IOGEN_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "sims/lpn/vta/include/vta/driver.h"

typedef void * VTAIOGenHandle;

/*!
 * \brief Allocate a device resource handle
 * \return The device handle.
 */
VTAIOGenHandle VTAIOGenAlloc();

/*!
 * \brief Free a device handle
 * \param handle The device handle to be freed.
 */
void VTAIOGenFree(VTAIOGenHandle handle);

/*!
 * \brief Launch the instructions block until done.
 * \param device The device handle.
 * \param insn_phy_addr The physical address of instruction stream.
 * \param insn_count Instruction count.
 * \param wait_cycles The maximum of cycles to wait
 *
 * \return 0 if running is successful, 1 if timeout.
 */
int VTAIOGenRun(VTAIOGenHandle device,
                 vta_phy_addr_t insn_phy_addr,
                 uint32_t insn_count,
                 uint32_t wait_cycles);

#ifdef __cplusplus
}
#endif
#endif  // VTA_DRIVER_H_

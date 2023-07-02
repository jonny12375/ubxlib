/*
 * Copyright 2019-2023 u-blox
 * 2023-    Jonathan Clark
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _U_CELL_SMS_H_
#define _U_CELL_SMS_H_

/* Only header files representing a direct and unavoidable
 * dependency between the API of this module and the API
 * of another module should be included here; otherwise
 * please keep #includes to your .c files. */

#include "u_device.h"
#include "u_gsm_pdu.h"

/** \addtogroup _cell
 *  @{
 */

/** @file
 * @brief This header file defines the APIs that obtain general
 * information from a cellular module (IMEI, etc.).
 * These functions are thread-safe with the proviso that a cellular
 * instance should not be accessed before it has been added or after
 * it has been removed.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */
typedef struct {
    uGsmPduSmsDeliver_t smsPdu;
    // char* message[160];
    // char* originator[30];
    // uint64_t serviceCenterTimestamp;
} uCellSms_t;
/* ----------------------------------------------------------------
 * FUNCTIONS
 * -------------------------------------------------------------- */

/** Read an SMS from storage
 *
 * @param cellHandle  the handle of the cellular instance.
 * @return            zero on success, negative error code on
 *                    failure.
 */
int32_t uCellSmsRead(uDeviceHandle_t cellHandle, int index, uCellSms_t *sms);

#ifdef __cplusplus
}
#endif

/** @}*/

#endif// _U_CELL_INFO_H_

// End of file

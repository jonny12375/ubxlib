/*
 * Copyright 2019-2023 u-blox
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

/* Only #includes of u_* and the C standard library are allowed here,
 * no platform stuff and no OS stuff.  Anything required from
 * the platform/OS must be brought in through u_port* to maintain
 * portability.
 */

/** @file
 * @brief Implementation of the info API for cellular.
 */

#ifdef U_CFG_OVERRIDE
#include "u_cfg_override.h"// For a customer's configuration override
#endif

#include "ctype.h"// isdigit()
#include "errno.h"
#include "limits.h"// INT_MAX
#include "stdbool.h"
#include "stddef.h"// NULL, size_t etc.
#include "stdint.h"// int32_t etc.
#include "stdlib.h"// strol(), atoi(), strol()
#include "string.h"// strlen()
#include "time.h"  // struct tm
#include <u_hex_bin_convert.h>

#include "u_cfg_sw.h"

#include "u_error_common.h"

#include "u_port_clib_platform_specific.h" // strtok_r() and, in some cases, isblank()
#include "u_port_clib_mktime64.h"
#include "u_port_debug.h"
#include "u_port_os.h"
#include "u_port_uart.h"

#include "u_at_client.h"

#include "u_device_shared.h"

#include "u_cell_module_type.h"
#include "u_cell_file.h"
#include "u_cell.h"         // Order is
#include "u_cell_net.h"     // important here
#include "u_cell_private.h" // don't change it
#include "u_cell_sms.h"

#include "u_gsm_pdu.h"

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */


/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS
 * -------------------------------------------------------------- */

int32_t uCellSmsRead(uDeviceHandle_t cellHandle, int index, uCellSms_t *sms)
{
    int32_t errorCodeOrSize = (int32_t) U_ERROR_COMMON_NOT_INITIALISED;
    uCellPrivateInstance_t *pInstance;
    uAtClientHandle_t atHandle;

    if (gUCellPrivateMutex != NULL)
    {
        U_PORT_MUTEX_LOCK(gUCellPrivateMutex);

        pInstance = pUCellPrivateGetInstance(cellHandle);
        errorCodeOrSize = (int32_t) U_ERROR_COMMON_INVALID_PARAMETER;
        if ((pInstance != NULL) && (index >= 0) && (sms != NULL))
        {
            atHandle = pInstance->atHandle;

            // CSDH=1 - show detailed SMS information
            uAtClientLock(atHandle);
            uAtClientCommandStart(atHandle, "AT+CSDH=");
            uAtClientWriteInt(atHandle, 1);
            uAtClientCommandStopReadResponse(atHandle);
            errorCodeOrSize = uAtClientUnlock(atHandle);
            if (errorCodeOrSize < 0)
            {
                return U_ERROR_COMMON_DEVICE_ERROR;
            }

            // CMGF=0 - Use PDU mode for SMS
            uAtClientLock(atHandle);
            uAtClientCommandStart(atHandle, "AT+CMGF=");
            uAtClientWriteInt(atHandle, 0);
            uAtClientCommandStopReadResponse(atHandle);
            errorCodeOrSize = uAtClientUnlock(atHandle);
            if (errorCodeOrSize < 0)
            {
                return U_ERROR_COMMON_DEVICE_ERROR;
            }

            uAtClientLock(atHandle);
            uAtClientCommandStart(atHandle, "AT+CMGR=");
            uAtClientWriteInt(atHandle, index);
            uAtClientCommandStop(atHandle);
            uAtClientResponseStart(atHandle, "+CMGR:");
            const int stat = uAtClientReadInt(atHandle);
            // Skip phonebook name
            uAtClientSkipParameters(atHandle, 1u);
            const int pdu_length = uAtClientReadInt(atHandle);
            uAtClientIgnoreStopTag(atHandle);
            char smsc_length_data[2];
            const int smsc_data = uAtClientReadBytes(atHandle, smsc_length_data, 2u, true);
            uAtClientRestoreStopTag(atHandle);
            char smsc_length=0;
            uHexToBin(smsc_length_data, 2u, &smsc_length);
            uint8_t data[200]= {0};
            data[0] = smsc_length;
            const int data_length = uAtClientReadHexData(atHandle, &data[1], smsc_length + pdu_length);
            errorCodeOrSize = uAtClientUnlock(atHandle);
            if ((data_length >= 0) && (errorCodeOrSize == 0))
            {
                errorCodeOrSize = 0;
                sms->smsPdu.stat = (uGsmPduSmsStat_t) stat;
                const int pdu_decode_res = uGsmPduDecodeSmsDeliver(data, data_length, &sms->smsPdu);
                if (pdu_decode_res < 0)
                {
                    errorCodeOrSize = pdu_decode_res;
                }
            }
            else
            {
                errorCodeOrSize = (int32_t) U_CELL_ERROR_AT;
                uPortLog("U_CELL_SMS: unable to read SMS\n");
            }
        }

        U_PORT_MUTEX_UNLOCK(gUCellPrivateMutex);
    }

    return errorCodeOrSize;
}

// Get the UTC time according to cellular.
int64_t get_timestamp(uDeviceHandle_t cellHandle)
{
    int64_t errorCodeOrValue = (int64_t) U_ERROR_COMMON_NOT_INITIALISED;
    uCellPrivateInstance_t *pInstance;
    uAtClientHandle_t atHandle;
    int64_t timeUtc;
    char timezoneSign = 0;
    char buffer[32];
    struct tm timeInfo;
    int32_t bytesRead;
    size_t offset = 0;

    if (gUCellPrivateMutex != NULL) {

        U_PORT_MUTEX_LOCK(gUCellPrivateMutex);

        pInstance = pUCellPrivateGetInstance(cellHandle);
        errorCodeOrValue = (int64_t) U_ERROR_COMMON_INVALID_PARAMETER;
        if (pInstance != NULL) {
            atHandle = pInstance->atHandle;
            uAtClientLock(atHandle);
            uAtClientCommandStart(atHandle, "AT+CCLK?");
            uAtClientCommandStop(atHandle);
            uAtClientResponseStart(atHandle, "+CCLK:");
            bytesRead = uAtClientReadString(atHandle, buffer,
                                            sizeof(buffer), false);
            uAtClientResponseStop(atHandle);
            errorCodeOrValue = uAtClientUnlock(atHandle);
            if ((bytesRead >= 17) && (errorCodeOrValue == 0)) {
                errorCodeOrValue = (int64_t) U_ERROR_COMMON_UNKNOWN;
                uPortLog("U_CELL_INFO: time is %s.\n", buffer);
                // The format of the returned string is
                // "yy/MM/dd,hh:mm:ss+TZ" but the +TZ may be omitted
                // Two-digit year converted to years since 1900
                offset = 0;
                buffer[offset + 2] = 0;
                timeInfo.tm_year = atoi(&(buffer[offset])) + 2000 - 1900;
                // Months converted to months since January
                offset = 3;
                buffer[offset + 2] = 0;
                timeInfo.tm_mon = atoi(&(buffer[offset])) - 1;
                // Day of month
                offset = 6;
                buffer[offset + 2] = 0;
                timeInfo.tm_mday = atoi(&(buffer[offset]));
                // Hours since midnight
                offset = 9;
                buffer[offset + 2] = 0;
                timeInfo.tm_hour = atoi(&(buffer[offset]));
                // Minutes after the hour
                offset = 12;
                buffer[offset + 2] = 0;
                timeInfo.tm_min = atoi(&(buffer[offset]));
                // Seconds after the hour
                // ...but, if there is timezone information,
                // save it before we obliterate the sign
                if (bytesRead >= 20) {
                    timezoneSign = buffer[17];
                }
                offset = 15;
                buffer[offset + 2] = 0;
                timeInfo.tm_sec = atoi(&(buffer[offset]));
                // Get the time in seconds from this
                timeUtc = mktime64(&timeInfo);
                offset = 17;
                if ((timeUtc >= 0) && (bytesRead >= 20) &&
                    ((timezoneSign == '+') || (timezoneSign == '-'))) {
                    // There's a timezone, expressed in 15 minute intervals,
                    // subtract it to get UTC
                    // Put the timezone sign back so that atoi() can handle it
                    buffer[offset] = timezoneSign;
                    buffer[offset + 3] = 0;
                    timeUtc -= atoi(&(buffer[offset])) * 15 * 60;
                }

                if (timeUtc >= 0) {
                    errorCodeOrValue = timeUtc;
                    uPortLog("U_CELL_INFO: UTC time is %d.\n", (int32_t) errorCodeOrValue);
                } else {
                    uPortLog("U_CELL_INFO: unable to calculate UTC time.\n");
                }
            } else {
                errorCodeOrValue = (int64_t) U_CELL_ERROR_AT;
                uPortLog("U_CELL_INFO: unable to read time with AT+CCLK.\n");
            }
        }

        U_PORT_MUTEX_UNLOCK(gUCellPrivateMutex);
    }

    return errorCodeOrValue;
}


// End of file

/*
* Copyright 2023 Jonathan Clark
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

#ifndef _U_GSM_PDU_H_
#define _U_GSM_PDU_H_

#include <stddef.h>
#include <stdint.h>

#define SMS_PDU_MAX_NUMBER_LENGTH 20u

typedef struct {
    uint8_t toa;
    uint8_t number_length;
    char number[SMS_PDU_MAX_NUMBER_LENGTH];
} uGsmPduSmscData_t;

typedef struct {
    uGsmPduSmscData_t smsc;
} uGsmPduSmsDeliver_t;

int uGsmPduDecodeSmsDeliver(const uint8_t *data, size_t length, uGsmPduSmsDeliver_t *sms);

#endif

#include "u_gsm_pdu.h"

#include "u_port_clib_mktime64.h"
#include "u_port_debug.h"

#include "u_error_common.h"

#include <time.h>

static int semi_octet_to_string(const uint8_t *data, size_t octet_count, char *output_string, uint8_t max_output_length)
{
    const char char_lookup[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};
    uint8_t *data_ptr = (uint8_t *) data;
    char *output_ptr = output_string;
    uint8_t semi_octet_count = octet_count * 2;
    if (semi_octet_count + 1 > max_output_length)
    {
        return U_ERROR_COMMON_NO_MEMORY;
    }

    while (semi_octet_count > 0)
    {
        const uint8_t semi_octet = semi_octet_count % 2 == 0 ? *data & 0x0Fu : (*data >> 4u) & 0x0Fu;
        if (semi_octet <= 9u)
        {
            *output_ptr = char_lookup[semi_octet];
            output_ptr++;
        }
        else if (semi_octet == 0x0Fu)
        {
            break;
        }

        semi_octet_count--;
        if (semi_octet_count == 0)
        {
            break;
        }
        if (semi_octet_count % 2 == 0)
        {
            data++;
        }
    }

    // Add null termination
    *output_ptr = '\0';
    output_ptr++;

    return output_ptr - output_string;
}

static int decode_smsc(const uint8_t *data, size_t length, uGsmPduNumber_t *smsc)
{
    const uint8_t smsc_length = data[0];
    if (length < smsc_length + 1)
    {
        return U_ERROR_COMMON_INVALID_PARAMETER;
    }

    smsc->toa = data[1];
    int res = semi_octet_to_string(&data[2], smsc_length - 1, smsc->number, sizeof(smsc->number));
    if (res < 0)
    {
        uPortLog("Failed to decode semi-octets\r\n");
        return res;
    }
    smsc->number_length = res;
    return smsc_length + 1;
}

static int decode_oa(const uint8_t *data, size_t length, uGsmPduNumber_t *oa)
{
    const uint8_t sender_semioctet_count = data[0u];
    oa->toa = data[1u];
    const uint8_t octet_count = sender_semioctet_count / 2 + sender_semioctet_count % 2;
    int res = semi_octet_to_string(&data[2], octet_count, oa->number, sizeof(oa->number));
    if (res < 0)
    {
        uPortLog("Failed to decode OA number\r\n");
        return res;
    }
    oa->number_length = res;

    return octet_count + 2;
}

/*!
 * Takes the swapped hex values from the date and converts it to an integer
 * e.g. 0x32 -> 23
 * @param in - input hex value
 * @return integer representation of the given data
 */
static uint8_t swapped_hex_to_val(uint8_t in)
{
    const uint8_t ones = (in & 0xF0) >> 4u;
    const uint8_t tens = (in & 0x0F);
    return ones + (tens * 10u);
}

static uint8_t swap_nibbles(uint8_t in)
{
    return ((in & 0xF0) >> 4u) | ((in & 0x0F) << 4u);
}

static int decode_time(const uint8_t *data, size_t length, int64_t *date_out)
{
    if (length < 7u)
    {
        return U_ERROR_COMMON_INVALID_PARAMETER;
    }

    struct tm timeInfo = {0};
    // tm_year must be years since 1900.
    timeInfo.tm_year = 100 + swapped_hex_to_val(data[0]);
    // tm_mon is 0: jan, 1: feb
    timeInfo.tm_mon = swapped_hex_to_val(data[1]) - 1;
    timeInfo.tm_mday = swapped_hex_to_val(data[2]);
    timeInfo.tm_hour = swapped_hex_to_val(data[3]);
    timeInfo.tm_min = swapped_hex_to_val(data[4]);
    timeInfo.tm_sec = swapped_hex_to_val(data[5]);

    int64_t time = mktime64(&timeInfo);
    // The timezone byte is weird, it's swapped hex, but also the top bit (after swapping nibbles) is the sign.
    const uint8_t tz_data = swap_nibbles(data[6]);
    const int8_t multiplier = tz_data & 0x80 ? -1 : 1;
    const uint8_t tz_offset_15min = swapped_hex_to_val(data[6] & 0xf7);
    const int64_t time_offset_s = multiplier * tz_offset_15min * 15ll * 60ll;
    *date_out = time - time_offset_s;

    return 0;
}

int uGsmPduDecodeSmsDeliver(const uint8_t *data, size_t length, uGsmPduSmsDeliver_t *sms)
{
    if (length == 0)
    {
        uPortLog("No data for PDU decoder\n");
        return U_ERROR_COMMON_INVALID_PARAMETER;
    }
    if (sms == NULL)
    {
        uPortLog("")
    }

    int errorCode = decode_smsc(data, length, &sms->smsc);
    if (errorCode < 0)
    {
        return errorCode;
    }

    const int smsc_data_length = errorCode;

    errorCode = decode_oa(&data[smsc_data_length + 1], length - (smsc_data_length + 1), &sms->oa);
    if (errorCode < 0)
    {
        return errorCode;
    }
    const uint8_t *tp_pid_ptr = &data[smsc_data_length + 1 + errorCode];
    sms->tp_pid = tp_pid_ptr[0];
    sms->dcs = (uGsmPduDcs_t) (tp_pid_ptr[1] & 0xC0u) >> 2u;
    errorCode = decode_time(&tp_pid_ptr[2], 7u, &sms->time);
    if (errorCode < 0)
    {
        return errorCode;
    }

    return 0;
}

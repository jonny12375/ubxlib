#include "u_gsm_pdu.h"

#include "u_port_debug.h"

#include "u_error_common.h"

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

static int decode_smsc(const uint8_t *data, size_t length, uGsmPduSmscData_t *smsc)
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
        uPortLog("Failed to decode semi-octets");
        return res;
    }
    smsc->number_length = res;
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

    return 0;
}

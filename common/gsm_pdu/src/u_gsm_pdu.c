#include "u_gsm_pdu.h"
#include <u_port_debug.h>

#include "u_error_common.h"

static int semi_octet_to_string(const uint8_t *data, size_t length, char *output_string)
{
}

static int decode_smsc(const uint8_t *data, size_t length, uGsmPduSmscData_t *smsc)
{

    const uint8_t smsc_length = data[0];
    if (length < smsc_length + 1)
    {
        return U_ERROR_COMMON_INVALID_PARAMETER;
    }
}

int uGsmPduDecodeSmsDeliver(const uint8_t *data, size_t length, uGsmPduSmsDeliver_t *sms)
{
    if (length == 0)
    {
        uPortLog("No data for PDU decoder\n");
        return U_ERROR_COMMON_INVALID_PARAMETER;
    }

    int errorCode = decode_smsc(data, length, &sms->smsc);
    if (errorCode < 0)
    {
        return errorCode;
    }
}

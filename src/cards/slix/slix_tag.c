#include "cards/slix/slix_tag.h"

#include "common/endian.h"
#include "protocols/nfcv/nfcv_profile.h"

#include <string.h>

#define SLIX_PASSWORD_BYTES 4u
#define SLIX_PASSWORD_ID_BYTES 1u
#define SLIX_PASSWORD_REQUEST_BYTES                                            \
    (SLIX_PASSWORD_ID_BYTES + SLIX_PASSWORD_BYTES)
/* Deterministic emulated challenge state; not a source of secret material. */
#define SLIX_RANDOM_INITIAL 0x1569u
#define SLIX_RANDOM_MULTIPLIER 109u
#define SLIX_RANDOM_INCREMENT 1021u

void slix_tag_reset_session(slix_tag_t *slix)
{
    memset(slix->password_match, 0, sizeof(slix->password_match));
    slix->random_valid = false;
}

void slix_tag_init(slix_tag_t *slix,
                   bool nxp_eas,
                   bool slix_l,
                   bool eas_locked,
                   bool privacy_mode,
                   const uint32_t passwords[SLIX_PASSWORD_TYPE_COUNT])
{
    memset(slix, 0, sizeof(*slix));
    slix->nxp_eas = nxp_eas;
    slix->slix_l = slix_l;
    slix->eas_active = nxp_eas;
    slix->eas_locked = eas_locked;
    slix->privacy_mode = privacy_mode;
    slix->pseudo_random = SLIX_RANDOM_INITIAL;
    memcpy(slix->passwords, passwords, sizeof(slix->passwords));
    slix_tag_reset_session(slix);
}

bool slix_tag_allows_request(const slix_tag_t *slix,
                             const nfcv_request_t *request)
{
    if (slix->destroyed ||
        (slix->privacy_mode &&
         request->command != NFCV_CMD_NXP_GET_RANDOM_NUMBER &&
         request->command != NFCV_CMD_NXP_SET_PASSWORD))
    {
        return false;
    }
    if (slix->password_match[SLIX_PASSWORD_TYPE_EAS_AFI])
    {
        return true;
    }
    switch (request->command)
    {
    case NFCV_CMD_WRITE_AFI:
    case NFCV_CMD_LOCK_AFI:
        return !slix->afi_password_protected;
    case NFCV_CMD_NXP_SET_EAS:
    case NFCV_CMD_NXP_RESET_EAS:
    case NFCV_CMD_NXP_LOCK_EAS:
        return !slix->eas_password_protected;
    default:
        return true;
    }
}

static bool slix_password_type_from_id(uint8_t id, slix_password_type_t *type)
{
    if (type == NULL)
    {
        return false;
    }
    switch (id)
    {
    case SLIX_PASSWORD_READ:
        *type = SLIX_PASSWORD_TYPE_READ;
        return true;
    case SLIX_PASSWORD_WRITE:
        *type = SLIX_PASSWORD_TYPE_WRITE;
        return true;
    case SLIX_PASSWORD_PRIVACY:
        *type = SLIX_PASSWORD_TYPE_PRIVACY;
        return true;
    case SLIX_PASSWORD_DESTROY:
        *type = SLIX_PASSWORD_TYPE_DESTROY;
        return true;
    case SLIX_PASSWORD_EAS_AFI:
        *type = SLIX_PASSWORD_TYPE_EAS_AFI;
        return true;
    default:
        return false;
    }
}

static uint32_t unmask_password(const slix_tag_t *slix,
                                uint32_t masked_password)
{
    const uint16_t random_swapped = __builtin_bswap16(slix->pseudo_random);
    const uint32_t mask = ((uint32_t)random_swapped << 16u) | random_swapped;
    return masked_password ^ mask;
}

static bool supports_password(const slix_tag_t *slix, slix_password_type_t type)
{
    return type == SLIX_PASSWORD_TYPE_EAS_AFI ||
           (slix->slix_l && (type == SLIX_PASSWORD_TYPE_PRIVACY ||
                             type == SLIX_PASSWORD_TYPE_DESTROY));
}

static bool set_password_from_request(slix_tag_t *slix,
                                      const nfcv_request_t *request)
{
    if (request->params_size_bytes != SLIX_PASSWORD_REQUEST_BYTES ||
        !slix->random_valid)
    {
        return false;
    }
    slix_password_type_t type;
    if (!slix_password_type_from_id(request->params[0], &type) ||
        !supports_password(slix, type) ||
        (type != SLIX_PASSWORD_TYPE_PRIVACY &&
         !(request->addressed || request->selected)))
    {
        return false;
    }
    const uint32_t password = unmask_password(
        slix, read_be32(&request->params[SLIX_PASSWORD_ID_BYTES]));
    if (password != slix->passwords[type])
    {
        return false;
    }
    slix->password_match[type] = true;
    if (type == SLIX_PASSWORD_TYPE_PRIVACY)
    {
        slix->privacy_mode = false;
    }
    return true;
}

static bool enable_privacy_from_request(slix_tag_t *slix,
                                        const nfcv_request_t *request)
{
    if (request->params_size_bytes != SLIX_PASSWORD_BYTES ||
        !slix->random_valid ||
        !supports_password(slix, SLIX_PASSWORD_TYPE_PRIVACY))
    {
        return false;
    }
    const uint32_t password = unmask_password(slix, read_be32(request->params));
    if (password != slix->passwords[SLIX_PASSWORD_TYPE_PRIVACY])
    {
        return false;
    }
    slix->password_match[SLIX_PASSWORD_TYPE_PRIVACY] = true;
    slix->privacy_mode = true;
    return true;
}

static bool write_password_from_request(slix_tag_t *slix,
                                        const nfcv_request_t *request)
{
    if (request->params_size_bytes != SLIX_PASSWORD_REQUEST_BYTES ||
        !(request->addressed || request->selected))
    {
        return false;
    }
    slix_password_type_t type;
    if (!slix_password_type_from_id(request->params[0], &type) ||
        !supports_password(slix, type) || !slix->password_match[type])
    {
        return false;
    }
    slix->passwords[type] = read_be32(&request->params[SLIX_PASSWORD_ID_BYTES]);
    slix->password_match[type] = false;
    return true;
}

static bool destroy_from_request(slix_tag_t *slix,
                                 const nfcv_request_t *request)
{
    if (!slix->slix_l || request->params_size_bytes != SLIX_PASSWORD_BYTES ||
        !slix->random_valid || !(request->addressed || request->selected))
    {
        return false;
    }
    const uint32_t password = unmask_password(slix, read_be32(request->params));
    if (password != slix->passwords[SLIX_PASSWORD_TYPE_DESTROY])
    {
        return false;
    }
    /* Unlike QUIET, this cannot be undone by RESET_TO_READY. Reload/reset
     * discards the emulated image state; no physical tag is destroyed. */
    slix->destroyed = true;
    return true;
}

static void reply_eas_alarm(const slix_tag_t *slix,
                            const nfcv_request_t *request,
                            nfcv_tag_response_t *response)
{
    /* SLIX-L section 9.4.3.12: ordinary flags/data/CRC framing.
     * The sequence is specified least-significant bit first. Selective
     * EAS requires ID state absent from the supported image format.
     * Disabled EAS and invalid/unsupported forms remain silent. */
    if (!slix->eas_active || request->option ||
        request->params_size_bytes != 0u)
    {
        return;
    }
    static const uint8_t sequence[] = {
        0x2f, 0xb3, 0x62, 0x70, 0xd5, 0xa7, 0x90, 0x7f, 0xe8, 0xb1, 0xc0,
        0x3c, 0xe2, 0x81, 0x49, 0x76, 0x82, 0xda, 0x9a, 0x86, 0x6f, 0x97,
        0x8b, 0xb0, 0xf1, 0x8c, 0xd1, 0x12, 0xa5, 0x72, 0x37, 0xef,
    };
    response->bytes[0] = NFCV_RES_OK;
    memcpy(&response->bytes[1], sequence, sizeof(sequence));
    response->size_bytes = 1u + sizeof(sequence);
    return;
}

static void reply_random_number(slix_tag_t *slix,
                                const nfcv_request_t *request,
                                nfcv_tag_response_t *response)
{
    if (request->params_size_bytes != 0u)
    {
        return;
    }
    slix->pseudo_random =
        (uint16_t)(slix->pseudo_random * SLIX_RANDOM_MULTIPLIER +
                   SLIX_RANDOM_INCREMENT);
    slix->random_valid = true;
    response->bytes[0] = NFCV_RES_OK;
    write_le16(&response->bytes[1], slix->pseudo_random);
    response->size_bytes = 1u + sizeof(slix->pseudo_random);
    return;
}

void slix_tag_handle(slix_tag_t *slix,
                     const nfcv_request_t *request,
                     nfcv_tag_response_t *response)
{
    if (request->inventory || (request->flags & NFCV_REQ_PROTOCOL_EXT) != 0u)
    {
        return;
    }
    if (!slix->nxp_eas)
    {
        if (request->addressed)
        {
            nfcv_reply_error(response, NFCV_ERR_CMD_NOT_SUPPORTED);
        }
        return;
    }

    switch (request->command)
    {
    case NFCV_CMD_NXP_SET_EAS:
        if (request->params_size_bytes != 0u)
        {
            break;
        }
        if (!slix->eas_locked)
        {
            slix->eas_active = true;
        }
        nfcv_reply_ok(response);
        break;
    case NFCV_CMD_NXP_RESET_EAS:
        if (request->params_size_bytes != 0u)
        {
            break;
        }
        if (!slix->eas_locked)
        {
            slix->eas_active = false;
        }
        nfcv_reply_ok(response);
        break;
    case NFCV_CMD_NXP_LOCK_EAS:
        if (request->params_size_bytes != 0u)
        {
            break;
        }
        slix->eas_locked = true;
        nfcv_reply_ok(response);
        break;
    case NFCV_CMD_NXP_EAS_ALARM:
        reply_eas_alarm(slix, request, response);
        break;
    case NFCV_CMD_NXP_GET_RANDOM_NUMBER:
        reply_random_number(slix, request, response);
        break;
    case NFCV_CMD_NXP_SET_PASSWORD:
        if (!set_password_from_request(slix, request))
        {
            break;
        }
        nfcv_reply_ok(response);
        break;
    case NFCV_CMD_NXP_ENABLE_PRIVACY:
        if (!enable_privacy_from_request(slix, request))
        {
            break;
        }
        nfcv_reply_ok(response);
        break;
    case NFCV_CMD_NXP_WRITE_PASSWORD:
        if (!write_password_from_request(slix, request))
        {
            break;
        }
        nfcv_reply_ok(response);
        break;
    case NFCV_CMD_NXP_PASSWORD_PROTECT_EAS_AFI:
        if (request->params_size_bytes != 0u ||
            !slix->password_match[SLIX_PASSWORD_TYPE_EAS_AFI])
        {
            break;
        }
        /* Option selects AFI rather than changing response timing. Each
         * protection is independent and lasts for this loaded image. */
        if (request->option)
        {
            slix->afi_password_protected = true;
        }
        else
        {
            slix->eas_password_protected = true;
        }
        nfcv_reply_ok(response);

        break;
    case NFCV_CMD_NXP_DESTROY:
        if (!slix->slix_l)
        {
            if (request->addressed)
            {
                nfcv_reply_error(response, NFCV_ERR_CMD_NOT_SUPPORTED);
            }
        }
        else if (destroy_from_request(slix, request))
        {
            nfcv_reply_ok(response);
        }
        break;
    default:
        if (request->addressed)
        {
            nfcv_reply_error(response, NFCV_ERR_CMD_NOT_SUPPORTED);
        }
        break;
    }
}

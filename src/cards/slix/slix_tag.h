#pragma once

#include "pn532_unleashed/nfcv_standard.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    SLIX_PASSWORD_TYPE_READ = 0u,
    SLIX_PASSWORD_TYPE_WRITE,
    SLIX_PASSWORD_TYPE_PRIVACY,
    SLIX_PASSWORD_TYPE_DESTROY,
    SLIX_PASSWORD_TYPE_EAS_AFI,
    SLIX_PASSWORD_TYPE_COUNT,
} slix_password_type_t;

typedef struct
{
    bool nxp_eas;
    bool slix_l;
    bool eas_active;
    bool eas_locked;
    bool eas_password_protected;
    bool afi_password_protected;
    bool privacy_mode;
    bool destroyed;
    bool random_valid;
    bool password_match[SLIX_PASSWORD_TYPE_COUNT];
    uint32_t passwords[SLIX_PASSWORD_TYPE_COUNT];
    uint16_t pseudo_random;
} slix_tag_t;

void slix_tag_init(slix_tag_t *slix,
                   bool nxp_eas,
                   bool slix_l,
                   bool eas_locked,
                   bool privacy_mode,
                   const uint32_t passwords[SLIX_PASSWORD_TYPE_COUNT]);
void slix_tag_reset_session(slix_tag_t *slix);
bool slix_tag_allows_request(const slix_tag_t *slix,
                             const nfcv_request_t *request);
void slix_tag_handle(slix_tag_t *slix,
                     const nfcv_request_t *request,
                     nfcv_tag_response_t *response);

/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef EMULATOR_BROKER_WIRE_H
#define EMULATOR_BROKER_WIRE_H
#include "broker.h"
#include <stddef.h>
#include <stdint.h>

#define MBW_HEADER_SIZE 48u
#define MBW_ADOPT_REQUEST_SIZE 256u
#define MBW_FRAME_REQUEST_SIZE 248u
#define MBW_ADOPT_RESPONSE_SIZE 296u
#define MBW_FRAME_RESPONSE_SIZE 668u
#define MBW_MAX_REQUEST_SIZE MBW_ADOPT_REQUEST_SIZE
#define MBW_MAX_RECORD_SIZE MBW_FRAME_RESPONSE_SIZE
#define MBW_ADOPT_REQUEST 1u
#define MBW_FRAME_REQUEST 2u
#define MBW_ADOPT_RESPONSE UINT32_C(0x80000001)
#define MBW_FRAME_RESPONSE UINT32_C(0x80000002)

enum mbw_result { MBW_OK = 0, MBW_INVALID = -1, MBW_CAPACITY = -2 };
struct mbw_header {
    uint32_t kind, length;
    uint8_t session[16];
    uint64_t exchange;
};
struct mbw_adopt_request { struct mb_state state; uint64_t producer_id; };
struct mbw_adopt_response {
    int32_t result;
    uint32_t provenance;
    struct mb_state state;
    struct mb_grant grant;
};
struct mbw_frame_response {
    int32_t result;
    uint32_t provenance;
    struct mb_receipt receipt; /* restore is absent from this wire protocol */
};
/* Header parser accepts exactly48 bytes and validates the known kind/length.
 * Full decoders accept exactly one canonical record, including no trailing byte.
 * Outputs are zero on decode failure. All input/output objects must be disjoint.
 * Payload state/identity shape is a dispatch policy, not a codec normalization.
 * An encoder refuses invalid header/provenance/enum or inadequate capacity before
 * writing; *written is zero on failure. No allocation or I/O occurs here. */
int mbw_decode_header(const uint8_t *, size_t, struct mbw_header *);
int mbw_encode_adopt_request(const struct mbw_header *, const struct mbw_adopt_request *, uint8_t *, size_t, size_t *);
int mbw_decode_adopt_request(const uint8_t *, size_t, struct mbw_header *, struct mbw_adopt_request *);
int mbw_encode_frame_request(const struct mbw_header *, const struct mb_request *, uint8_t *, size_t, size_t *);
int mbw_decode_frame_request(const uint8_t *, size_t, struct mbw_header *, struct mb_request *);
int mbw_encode_adopt_response(const struct mbw_header *, const struct mbw_adopt_response *, uint8_t *, size_t, size_t *);
int mbw_decode_adopt_response(const uint8_t *, size_t, struct mbw_header *, struct mbw_adopt_response *);
int mbw_encode_frame_response(const struct mbw_header *, const struct mbw_frame_response *, uint8_t *, size_t, size_t *);
int mbw_decode_frame_response(const uint8_t *, size_t, struct mbw_header *, struct mbw_frame_response *);
#endif

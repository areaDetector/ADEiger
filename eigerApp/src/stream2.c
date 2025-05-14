#define __STDC_WANT_IEC_60559_TYPES_EXT__
#include "stream2.h"

#include <assert.h>
#include <float.h>
#if FLT16_MANT_DIG > 0 || __FLT16_MANT_DIG__ > 0
#else
#include <immintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#endif
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cbor.h"

enum { MAX_KEY_LEN = 64 };

static const CborTag MULTI_DIMENSIONAL_ARRAY_ROW_MAJOR = 40;
static const CborTag DECTRIS_COMPRESSION = 56500;

static uint64_t read_u64_be(const uint8_t* buf) {
    return ((uint64_t)buf[0] << 56) | ((uint64_t)buf[1] << 48) |
           ((uint64_t)buf[2] << 40) | ((uint64_t)buf[3] << 32) |
           ((uint64_t)buf[4] << 24) | ((uint64_t)buf[5] << 16) |
           ((uint64_t)buf[6] << 8) | (uint64_t)buf[7];
}

static enum stream2_result CBOR_RESULT(CborError e) {
    switch (e) {
        case CborNoError:
            return STREAM2_OK;
        case CborErrorUnknownLength:
        case CborErrorDataTooLarge:
            return STREAM2_ERROR_PARSE;
        case CborErrorOutOfMemory:
            return STREAM2_ERROR_OUT_OF_MEMORY;
        default:
            return STREAM2_ERROR_DECODE;
    }
}

static enum stream2_result consume_byte_string_nocopy(const CborValue* it,
                                                      const uint8_t** bstr,
                                                      size_t* bstr_len,
                                                      CborValue* next) {
    enum stream2_result r;

    assert(cbor_value_is_byte_string(it));

    if ((r = CBOR_RESULT(cbor_value_get_string_length(it, bstr_len))))
        return r;

    const uint8_t* ptr = cbor_value_get_next_byte(it);
    assert(*ptr >= 0x40 && *ptr <= 0x5b);
    switch (*ptr++) {
        case 0x58:
            ptr += 1;
            break;
        case 0x59:
            ptr += 2;
            break;
        case 0x5a:
            ptr += 4;
            break;
        case 0x5b:
            ptr += 8;
            break;
    }
    *bstr = ptr;

    if (next) {
        *next = *it;
        if ((r = CBOR_RESULT(cbor_value_advance(next))))
            return r;
    }
    return STREAM2_OK;
}

static enum stream2_result parse_key(CborValue* it, char key[MAX_KEY_LEN]) {
    if (!cbor_value_is_text_string(it))
        return STREAM2_ERROR_PARSE;

    size_t key_len = MAX_KEY_LEN;
    CborError e = cbor_value_copy_text_string(it, key, &key_len, it);
    if (e == CborErrorOutOfMemory || key_len == MAX_KEY_LEN) {
        // The key is longer than any we suppport. Return the empty key which
        // should be handled by the caller like other unknown keys.
        key[0] = '\0';
        return STREAM2_OK;
    }
    return CBOR_RESULT(e);
}

static enum stream2_result parse_tag(CborValue* it, CborTag* value) {
    enum stream2_result r;

    if (!cbor_value_is_tag(it))
        return STREAM2_ERROR_PARSE;

    if ((r = CBOR_RESULT(cbor_value_get_tag(it, value))))
        return r;

    return CBOR_RESULT(cbor_value_advance_fixed(it));
}

static enum stream2_result parse_bool(CborValue* it, bool* value) {
    enum stream2_result r;

    if (!cbor_value_is_boolean(it))
        return STREAM2_ERROR_PARSE;

    if ((r = CBOR_RESULT(cbor_value_get_boolean(it, value))))
        return r;

    return CBOR_RESULT(cbor_value_advance_fixed(it));
}

static float half_to_float(uint16_t x) {
#if FLT16_MANT_DIG > 0 || __FLT16_MANT_DIG__ > 0
    _Float16 f;
    memcpy(&f, &x, 2);
    return (float)f;
#else
    return _mm_cvtss_f32(_mm_cvtph_ps(_mm_cvtsi32_si128(x)));
#endif
}

static enum stream2_result parse_double(CborValue* it, double* value) {
    enum stream2_result r;

    if (cbor_value_is_half_float(it)) {
        uint16_t h;
        if ((r = CBOR_RESULT(cbor_value_get_half_float(it, &h))))
            return r;
        *value = (double)half_to_float(h);
    } else if (cbor_value_is_float(it)) {
        float f;
        if ((r = CBOR_RESULT(cbor_value_get_float(it, &f))))
            return r;
        *value = (double)f;
    } else if (cbor_value_is_double(it)) {
        if ((r = CBOR_RESULT(cbor_value_get_double(it, value))))
            return r;
    } else {
        return STREAM2_ERROR_PARSE;
    }

    return CBOR_RESULT(cbor_value_advance_fixed(it));
}

static enum stream2_result parse_uint64(CborValue* it, uint64_t* value) {
    enum stream2_result r;

    if (!cbor_value_is_unsigned_integer(it))
        return STREAM2_ERROR_PARSE;

    if ((r = CBOR_RESULT(cbor_value_get_uint64(it, value))))
        return r;

    return CBOR_RESULT(cbor_value_advance_fixed(it));
}

static enum stream2_result parse_text_string(CborValue* it, char** tstr) {
    if (!cbor_value_is_text_string(it))
        return STREAM2_ERROR_PARSE;

    size_t len;
    return CBOR_RESULT(cbor_value_dup_text_string(it, tstr, &len, it));
}

static enum stream2_result parse_array_2_uint64(CborValue* it,
                                                uint64_t array[2]) {
    enum stream2_result r;

    if (!cbor_value_is_array(it))
        return STREAM2_ERROR_PARSE;

    size_t len;
    if ((r = CBOR_RESULT(cbor_value_get_array_length(it, &len))))
        return r;

    if (len != 2)
        return STREAM2_ERROR_PARSE;

    CborValue elt;
    if ((r = CBOR_RESULT(cbor_value_enter_container(it, &elt))))
        return r;

    for (size_t i = 0; i < len; i++) {
        if ((r = parse_uint64(&elt, &array[i])))
            return r;
    }

    return CBOR_RESULT(cbor_value_leave_container(it, &elt));
}

static enum stream2_result parse_dectris_compression(
        CborValue* it,
        struct stream2_compression* compression,
        const uint8_t** bstr,
        size_t* bstr_len) {
    enum stream2_result r;

    CborTag tag;
    if ((r = parse_tag(it, &tag)))
        return r;

    if (tag != DECTRIS_COMPRESSION)
        return STREAM2_ERROR_PARSE;

    if (!cbor_value_is_array(it))
        return STREAM2_ERROR_PARSE;

    size_t len;
    if ((r = CBOR_RESULT(cbor_value_get_array_length(it, &len))))
        return r;

    if (len != 3)
        return STREAM2_ERROR_PARSE;

    CborValue elt;
    if ((r = CBOR_RESULT(cbor_value_enter_container(it, &elt))))
        return r;

    if ((r = parse_text_string(&elt, &compression->algorithm)))
        return r;

    if ((r = parse_uint64(&elt, &compression->elem_size)))
        return r;

    if (!cbor_value_is_byte_string(&elt))
        return STREAM2_ERROR_PARSE;

    if ((r = consume_byte_string_nocopy(&elt, bstr, bstr_len, &elt)))
        return r;

    // https://github.com/dectris/compression/blob/v0.3.0/src/compression.c#L46
    if (strcmp(compression->algorithm, "bslz4") == 0 ||
        strcmp(compression->algorithm, "lz4") == 0)
    {
        if (*bstr_len < 12)
            return STREAM2_ERROR_DECODE;
        compression->orig_size = read_u64_be(*bstr);
    } else {
        return STREAM2_ERROR_NOT_IMPLEMENTED;
    }

    return CBOR_RESULT(cbor_value_leave_container(it, &elt));
}

static enum stream2_result parse_bytes(CborValue* it,
                                       struct stream2_bytes* bytes) {
    enum stream2_result r;

    if (cbor_value_is_tag(it)) {
        CborTag tag;
        if ((r = CBOR_RESULT(cbor_value_get_tag(it, &tag))))
            return r;

        if (tag == DECTRIS_COMPRESSION) {
            return parse_dectris_compression(it, &bytes->compression,
                                             &bytes->ptr, &bytes->len);
        } else {
            return STREAM2_ERROR_PARSE;
        }
    }

    if (!cbor_value_is_byte_string(it))
        return STREAM2_ERROR_PARSE;

    bytes->compression.algorithm = NULL;
    bytes->compression.elem_size = 0;
    bytes->compression.orig_size = 0;

    return consume_byte_string_nocopy(it, &bytes->ptr, &bytes->len, it);
}

// Parses a typed array from [RFC 8746 section 2].
//
// [RFC 8746 section 2]:
// https://www.rfc-editor.org/rfc/rfc8746.html#name-typed-arrays
static enum stream2_result parse_typed_array(CborValue* it,
                                             struct stream2_typed_array* array,
                                             uint64_t* len) {
    enum stream2_result r;

    if ((r = parse_tag(it, &array->tag)))
        return r;

    if ((r = parse_bytes(it, &array->data)))
        return r;

    uint64_t elem_size;
    if ((r = stream2_typed_array_elem_size(array, &elem_size)))
        return r;

    uint64_t size;
    if (array->data.compression.algorithm == NULL)
        size = array->data.len;
    else
        size = array->data.compression.orig_size;

    if (size % elem_size != 0)
        return STREAM2_ERROR_PARSE;

    *len = size / elem_size;
    return STREAM2_OK;
}

// Parses a multi-dimensional array from [RFC 8746 section 3.1.1].
//
// [RFC 8746 section 3.1.1]:
// https://www.rfc-editor.org/rfc/rfc8746.html#name-row-major-order
static enum stream2_result parse_multidim_array(
        CborValue* it,
        struct stream2_multidim_array* multidim) {
    enum stream2_result r;

    CborTag tag;
    if ((r = parse_tag(it, &tag)))
        return r;

    if (tag != MULTI_DIMENSIONAL_ARRAY_ROW_MAJOR)
        return STREAM2_ERROR_PARSE;

    if (!cbor_value_is_array(it))
        return STREAM2_ERROR_PARSE;

    size_t len;
    if ((r = CBOR_RESULT(cbor_value_get_array_length(it, &len))))
        return r;

    if (len != 2)
        return STREAM2_ERROR_PARSE;

    CborValue elt;
    if ((r = CBOR_RESULT(cbor_value_enter_container(it, &elt))))
        return r;

    if ((r = parse_array_2_uint64(&elt, multidim->dim)))
        return r;

    uint64_t array_len;
    if ((r = parse_typed_array(&elt, &multidim->array, &array_len)))
        return r;

    if (multidim->dim[0] * multidim->dim[1] != array_len)
        return STREAM2_ERROR_PARSE;

    return CBOR_RESULT(cbor_value_leave_container(it, &elt));
}

static enum stream2_result parse_goniometer_axis(
        CborValue* it,
        struct stream2_goniometer_axis* axis) {
    enum stream2_result r;

    if (!cbor_value_is_map(it))
        return STREAM2_ERROR_PARSE;

    CborValue field;
    if ((r = CBOR_RESULT(cbor_value_enter_container(it, &field))))
        return r;

    while (cbor_value_is_valid(&field)) {
        char key[MAX_KEY_LEN];
        if ((r = parse_key(&field, key)))
            return r;

        if ((r = CBOR_RESULT(cbor_value_skip_tag(&field))))
            return r;

        if (strcmp(key, "increment") == 0) {
            if ((r = parse_double(&field, &axis->increment)))
                return r;
        } else if (strcmp(key, "start") == 0) {
            if ((r = parse_double(&field, &axis->start)))
                return r;
        } else {
            if ((r = CBOR_RESULT(cbor_value_advance(&field))))
                return r;
        }
    }

    return CBOR_RESULT(cbor_value_leave_container(it, &field));
}

static enum stream2_result parse_goniometer(
        CborValue* it,
        struct stream2_goniometer* goniometer) {
    enum stream2_result r;

    if (!cbor_value_is_map(it))
        return STREAM2_ERROR_PARSE;

    CborValue field;
    if ((r = CBOR_RESULT(cbor_value_enter_container(it, &field))))
        return r;

    while (cbor_value_is_valid(&field)) {
        char key[MAX_KEY_LEN];
        if ((r = parse_key(&field, key)))
            return r;

        if ((r = CBOR_RESULT(cbor_value_skip_tag(&field))))
            return r;

        if (strcmp(key, "chi") == 0) {
            if ((r = parse_goniometer_axis(&field, &goniometer->chi)))
                return r;
        } else if (strcmp(key, "kappa") == 0) {
            if ((r = parse_goniometer_axis(&field, &goniometer->kappa)))
                return r;
        } else if (strcmp(key, "omega") == 0) {
            if ((r = parse_goniometer_axis(&field, &goniometer->omega)))
                return r;
        } else if (strcmp(key, "phi") == 0) {
            if ((r = parse_goniometer_axis(&field, &goniometer->phi)))
                return r;
        } else if (strcmp(key, "two_theta") == 0) {
            if ((r = parse_goniometer_axis(&field, &goniometer->two_theta)))
                return r;
        } else {
            if ((r = CBOR_RESULT(cbor_value_advance(&field))))
                return r;
        }
    }

    return CBOR_RESULT(cbor_value_leave_container(it, &field));
}

static enum stream2_result parse_user_data(
        CborValue* it,
        struct stream2_user_data* user_data) {
    enum stream2_result r;

    const uint8_t* ptr = cbor_value_get_next_byte(it);

    if ((r = CBOR_RESULT(cbor_value_advance(it))))
        return r;

    user_data->ptr = ptr;
    user_data->len = cbor_value_get_next_byte(it) - ptr;

    return STREAM2_OK;
}

static enum stream2_result parse_start_msg(CborValue* it,
                                           struct stream2_msg** msg_out) {
    enum stream2_result r;

    struct stream2_start_msg* msg = calloc(1, sizeof(struct stream2_start_msg));
    *msg_out = (struct stream2_msg*)msg;
    if (msg == NULL)
        return STREAM2_ERROR_OUT_OF_MEMORY;

    msg->type = STREAM2_MSG_START;
    msg->countrate_correction_lookup_table.tag = UINT64_MAX;

    while (cbor_value_is_valid(it)) {
        char key[MAX_KEY_LEN];
        if ((r = parse_key(it, key)))
            return r;

        // skip any tag for a value, except where verified
        if (strcmp(key, "countrate_correction_lookup_table") != 0) {
            if ((r = CBOR_RESULT(cbor_value_skip_tag(it))))
                return r;
        }

        if (strcmp(key, "series_id") == 0) {
            if ((r = parse_uint64(it, &msg->series_id)))
                return r;
        } else if (strcmp(key, "series_unique_id") == 0) {
            if ((r = parse_text_string(it, &msg->series_unique_id)))
                return r;
        } else if (strcmp(key, "arm_date") == 0) {
            if ((r = parse_text_string(it, &msg->arm_date)))
                return r;
        } else if (strcmp(key, "beam_center_x") == 0) {
            if ((r = parse_double(it, &msg->beam_center_x)))
                return r;
        } else if (strcmp(key, "beam_center_y") == 0) {
            if ((r = parse_double(it, &msg->beam_center_y)))
                return r;
        } else if (strcmp(key, "channels") == 0) {
            if (!cbor_value_is_array(it))
                return STREAM2_ERROR_PARSE;

            size_t len;
            if ((r = CBOR_RESULT(cbor_value_get_array_length(it, &len))))
                return r;

            msg->channels.ptr = calloc(len, sizeof(char*));
            if (msg->channels.ptr == NULL)
                return STREAM2_ERROR_OUT_OF_MEMORY;

            msg->channels.len = len;

            CborValue elt;
            if ((r = CBOR_RESULT(cbor_value_enter_container(it, &elt))))
                return r;

            for (size_t i = 0; i < len; i++) {
                if ((r = parse_text_string(&elt, &msg->channels.ptr[i])))
                    return r;
            }

            if ((r = CBOR_RESULT(cbor_value_leave_container(it, &elt))))
                return r;
        } else if (strcmp(key, "count_time") == 0) {
            if ((r = parse_double(it, &msg->count_time)))
                return r;
        } else if (strcmp(key, "countrate_correction_enabled") == 0) {
            if ((r = parse_bool(it, &msg->countrate_correction_enabled)))
                return r;
        } else if (strcmp(key, "countrate_correction_lookup_table") == 0) {
            uint64_t len;
            if ((r = parse_typed_array(
                         it, &msg->countrate_correction_lookup_table, &len)))
                return r;
        } else if (strcmp(key, "detector_description") == 0) {
            if ((r = parse_text_string(it, &msg->detector_description)))
                return r;
        } else if (strcmp(key, "detector_serial_number") == 0) {
            if ((r = parse_text_string(it, &msg->detector_serial_number)))
                return r;
        } else if (strcmp(key, "detector_translation") == 0) {
            if (!cbor_value_is_array(it))
                return STREAM2_ERROR_PARSE;

            size_t len;
            if ((r = CBOR_RESULT(cbor_value_get_array_length(it, &len))))
                return r;

            if (len != 3)
                return STREAM2_ERROR_PARSE;

            CborValue elt;
            if ((r = CBOR_RESULT(cbor_value_enter_container(it, &elt))))
                return r;

            for (size_t i = 0; i < len; i++) {
                if ((r = parse_double(&elt, &msg->detector_translation[i])))
                    return r;
            }

            if ((r = CBOR_RESULT(cbor_value_leave_container(it, &elt))))
                return r;
        } else if (strcmp(key, "flatfield") == 0) {
            if (!cbor_value_is_map(it))
                return STREAM2_ERROR_PARSE;

            size_t len;
            if ((r = CBOR_RESULT(cbor_value_get_map_length(it, &len))))
                return r;

            msg->flatfield.ptr = calloc(len, sizeof(struct stream2_flatfield));
            if (msg->flatfield.ptr == NULL)
                return STREAM2_ERROR_OUT_OF_MEMORY;

            msg->flatfield.len = len;

            CborValue field;
            if ((r = CBOR_RESULT(cbor_value_enter_container(it, &field))))
                return r;

            for (size_t i = 0; i < len; i++) {
                if ((r = parse_text_string(&field,
                                           &msg->flatfield.ptr[i].channel)))
                    return r;

                if ((r = parse_multidim_array(
                             &field, &msg->flatfield.ptr[i].flatfield)))
                    return r;
            }

            if ((r = CBOR_RESULT(cbor_value_leave_container(it, &field))))
                return r;
        } else if (strcmp(key, "flatfield_enabled") == 0) {
            if ((r = parse_bool(it, &msg->flatfield_enabled)))
                return r;
        } else if (strcmp(key, "frame_time") == 0) {
            if ((r = parse_double(it, &msg->frame_time)))
                return r;
        } else if (strcmp(key, "goniometer") == 0) {
            if ((r = parse_goniometer(it, &msg->goniometer)))
                return r;
        } else if (strcmp(key, "image_dtype") == 0) {
            if ((r = parse_text_string(it, &msg->image_dtype)))
                return r;
        } else if (strcmp(key, "image_size_x") == 0) {
            if ((r = parse_uint64(it, &msg->image_size_x)))
                return r;
        } else if (strcmp(key, "image_size_y") == 0) {
            if ((r = parse_uint64(it, &msg->image_size_y)))
                return r;
        } else if (strcmp(key, "incident_energy") == 0) {
            if ((r = parse_double(it, &msg->incident_energy)))
                return r;
        } else if (strcmp(key, "incident_wavelength") == 0) {
            if ((r = parse_double(it, &msg->incident_wavelength)))
                return r;
        } else if (strcmp(key, "number_of_images") == 0) {
            if ((r = parse_uint64(it, &msg->number_of_images)))
                return r;
        } else if (strcmp(key, "pixel_mask") == 0) {
            if (!cbor_value_is_map(it))
                return STREAM2_ERROR_PARSE;

            size_t len;
            if ((r = CBOR_RESULT(cbor_value_get_map_length(it, &len))))
                return r;

            msg->pixel_mask.ptr =
                    calloc(len, sizeof(struct stream2_pixel_mask));
            if (msg->pixel_mask.ptr == NULL)
                return STREAM2_ERROR_OUT_OF_MEMORY;

            msg->pixel_mask.len = len;

            CborValue field;
            if ((r = CBOR_RESULT(cbor_value_enter_container(it, &field))))
                return r;

            for (size_t i = 0; i < len; i++) {
                if ((r = parse_text_string(&field,
                                           &msg->pixel_mask.ptr[i].channel)))
                    return r;

                if ((r = parse_multidim_array(
                             &field, &msg->pixel_mask.ptr[i].pixel_mask)))
                    return r;
            }

            if ((r = CBOR_RESULT(cbor_value_leave_container(it, &field))))
                return r;
        } else if (strcmp(key, "pixel_mask_enabled") == 0) {
            if ((r = parse_bool(it, &msg->pixel_mask_enabled)))
                return r;
        } else if (strcmp(key, "pixel_size_x") == 0) {
            if ((r = parse_double(it, &msg->pixel_size_x)))
                return r;
        } else if (strcmp(key, "pixel_size_y") == 0) {
            if ((r = parse_double(it, &msg->pixel_size_y)))
                return r;
        } else if (strcmp(key, "saturation_value") == 0) {
            if ((r = parse_uint64(it, &msg->saturation_value)))
                return r;
        } else if (strcmp(key, "sensor_material") == 0) {
            if ((r = parse_text_string(it, &msg->sensor_material)))
                return r;
        } else if (strcmp(key, "sensor_thickness") == 0) {
            if ((r = parse_double(it, &msg->sensor_thickness)))
                return r;
        } else if (strcmp(key, "threshold_energy") == 0) {
            if (!cbor_value_is_map(it))
                return STREAM2_ERROR_PARSE;

            size_t len;
            if ((r = CBOR_RESULT(cbor_value_get_map_length(it, &len))))
                return r;

            msg->threshold_energy.ptr =
                    calloc(len, sizeof(struct stream2_threshold_energy));
            if (msg->threshold_energy.ptr == NULL)
                return STREAM2_ERROR_OUT_OF_MEMORY;

            msg->threshold_energy.len = len;

            CborValue field;
            if ((r = CBOR_RESULT(cbor_value_enter_container(it, &field))))
                return r;

            for (size_t i = 0; i < len; i++) {
                if ((r = parse_text_string(
                             &field, &msg->threshold_energy.ptr[i].channel)))
                    return r;

                if ((r = parse_double(&field,
                                      &msg->threshold_energy.ptr[i].energy)))
                    return r;
            }

            if ((r = CBOR_RESULT(cbor_value_leave_container(it, &field))))
                return r;
        } else if (strcmp(key, "user_data") == 0) {
            if ((r = parse_user_data(it, &msg->user_data)))
                return r;
        } else if (strcmp(key, "virtual_pixel_interpolation_enabled") == 0) {
            if ((r = parse_bool(it, &msg->virtual_pixel_interpolation_enabled)))
                return r;
        } else {
            if ((r = CBOR_RESULT(cbor_value_advance(it))))
                return r;
        }
    }
    return STREAM2_OK;
}

static enum stream2_result parse_image_msg(CborValue* it,
                                           struct stream2_msg** msg_out) {
    enum stream2_result r;

    struct stream2_image_msg* msg = calloc(1, sizeof(struct stream2_image_msg));
    *msg_out = (struct stream2_msg*)msg;
    if (msg == NULL)
        return STREAM2_ERROR_OUT_OF_MEMORY;

    msg->type = STREAM2_MSG_IMAGE;

    while (cbor_value_is_valid(it)) {
        char key[MAX_KEY_LEN];
        if ((r = parse_key(it, key)))
            return r;

        if ((r = CBOR_RESULT(cbor_value_skip_tag(it))))
            return r;

        if (strcmp(key, "series_id") == 0) {
            if ((r = parse_uint64(it, &msg->series_id)))
                return r;
        } else if (strcmp(key, "series_unique_id") == 0) {
            if ((r = parse_text_string(it, &msg->series_unique_id)))
                return r;
        } else if (strcmp(key, "image_id") == 0) {
            if ((r = parse_uint64(it, &msg->image_id)))
                return r;
        } else if (strcmp(key, "real_time") == 0) {
            if ((r = parse_array_2_uint64(it, msg->real_time)))
                return r;
        } else if (strcmp(key, "series_date") == 0) {
            if ((r = parse_text_string(it, &msg->series_date)))
                return r;
        } else if (strcmp(key, "start_time") == 0) {
            if ((r = parse_array_2_uint64(it, msg->start_time)))
                return r;
        } else if (strcmp(key, "stop_time") == 0) {
            if ((r = parse_array_2_uint64(it, msg->stop_time)))
                return r;
        } else if (strcmp(key, "user_data") == 0) {
            if ((r = parse_user_data(it, &msg->user_data)))
                return r;
        } else if (strcmp(key, "data") == 0) {
            if (!cbor_value_is_map(it))
                return STREAM2_ERROR_PARSE;

            size_t len;
            if ((r = CBOR_RESULT(cbor_value_get_map_length(it, &len))))
                return r;

            msg->data.ptr = calloc(len, sizeof(struct stream2_image_data));
            if (msg->data.ptr == NULL)
                return STREAM2_ERROR_OUT_OF_MEMORY;

            msg->data.len = len;

            CborValue field;
            if ((r = CBOR_RESULT(cbor_value_enter_container(it, &field))))
                return r;

            for (size_t i = 0; i < len; i++) {
                if ((r = parse_text_string(&field, &msg->data.ptr[i].channel)))
                    return r;

                if ((r = parse_multidim_array(&field, &msg->data.ptr[i].data)))
                    return r;
            }

            if ((r = CBOR_RESULT(cbor_value_leave_container(it, &field))))
                return r;
        } else {
            if ((r = CBOR_RESULT(cbor_value_advance(it))))
                return r;
        }
    }
    return STREAM2_OK;
}

static enum stream2_result parse_end_msg(CborValue* it,
                                         struct stream2_msg** msg_out) {
    enum stream2_result r;

    struct stream2_end_msg* msg = calloc(1, sizeof(struct stream2_end_msg));
    *msg_out = (struct stream2_msg*)msg;
    if (msg == NULL)
        return STREAM2_ERROR_OUT_OF_MEMORY;

    msg->type = STREAM2_MSG_END;

    while (cbor_value_is_valid(it)) {
        char key[MAX_KEY_LEN];
        if ((r = parse_key(it, key)))
            return r;

        if ((r = CBOR_RESULT(cbor_value_skip_tag(it))))
            return r;

        if (strcmp(key, "series_id") == 0) {
            if ((r = parse_uint64(it, &msg->series_id)))
                return r;
        } else if (strcmp(key, "series_unique_id") == 0) {
            if ((r = parse_text_string(it, &msg->series_unique_id)))
                return r;
        } else {
            if ((r = CBOR_RESULT(cbor_value_advance(it))))
                return r;
        }
    }
    return STREAM2_OK;
}

static enum stream2_result parse_msg_type(CborValue* it,
                                          char type[MAX_KEY_LEN]) {
    enum stream2_result r;

    char key[MAX_KEY_LEN];
    if ((r = parse_key(it, key)))
        return r;

    if (strcmp(key, "type") != 0)
        return STREAM2_ERROR_PARSE;

    if ((r = CBOR_RESULT(cbor_value_skip_tag(it))))
        return r;

    return parse_key(it, type);
}

static enum stream2_result parse_msg(const uint8_t* buffer,
                                     size_t size,
                                     struct stream2_msg** msg_out) {
    enum stream2_result r;

    // https://www.rfc-editor.org/rfc/rfc8949.html#name-self-described-cbor
    const uint8_t MAGIC[3] = {0xd9, 0xd9, 0xf7};
    if (size < sizeof(MAGIC) || memcmp(buffer, MAGIC, sizeof(MAGIC)) != 0)
        return STREAM2_ERROR_SIGNATURE;

    buffer += sizeof(MAGIC);
    size -= sizeof(MAGIC);

    CborParser parser;
    CborValue it;
    if ((r = CBOR_RESULT(cbor_parser_init(buffer, size, 0, &parser, &it))))
        return r;

    if (!cbor_value_is_map(&it))
        return STREAM2_ERROR_PARSE;

    CborValue field;
    if ((r = CBOR_RESULT(cbor_value_enter_container(&it, &field))))
        return r;

    char type[MAX_KEY_LEN];
    if ((r = parse_msg_type(&field, type)))
        return r;

    if (strcmp(type, "start") == 0) {
        if ((r = parse_start_msg(&field, msg_out)))
            return r;
    } else if (strcmp(type, "image") == 0) {
        if ((r = parse_image_msg(&field, msg_out)))
            return r;
    } else if (strcmp(type, "end") == 0) {
        if ((r = parse_end_msg(&field, msg_out)))
            return r;
    } else {
        return STREAM2_ERROR_PARSE;
    }

    return CBOR_RESULT(cbor_value_leave_container(&it, &field));
}

enum stream2_result stream2_parse_msg(const uint8_t* buffer,
                                      size_t size,
                                      struct stream2_msg** msg_out) {
    enum stream2_result r;

    *msg_out = NULL;
    if ((r = parse_msg(buffer, size, msg_out))) {
        if (*msg_out) {
            stream2_free_msg(*msg_out);
            *msg_out = NULL;
        }
        return r;
    }
    return STREAM2_OK;
}

static void free_start_msg(struct stream2_start_msg* msg) {
    free(msg->arm_date);
    for (size_t i = 0; i < msg->channels.len; i++)
        free(msg->channels.ptr[i]);
    free(msg->channels.ptr);
    free(msg->countrate_correction_lookup_table.data.compression.algorithm);
    free(msg->detector_description);
    free(msg->detector_serial_number);
    for (size_t i = 0; i < msg->flatfield.len; i++) {
        free(msg->flatfield.ptr[i].channel);
        free(msg->flatfield.ptr[i].flatfield.array.data.compression.algorithm);
    }
    free(msg->flatfield.ptr);
    free(msg->image_dtype);
    for (size_t i = 0; i < msg->pixel_mask.len; i++) {
        free(msg->pixel_mask.ptr[i].channel);
        free(msg->pixel_mask.ptr[i]
                     .pixel_mask.array.data.compression.algorithm);
    }
    free(msg->pixel_mask.ptr);
    free(msg->sensor_material);
    for (size_t i = 0; i < msg->threshold_energy.len; i++)
        free(msg->threshold_energy.ptr[i].channel);
    free(msg->threshold_energy.ptr);
}

static void free_image_msg(struct stream2_image_msg* msg) {
    free(msg->series_date);
    for (size_t i = 0; i < msg->data.len; i++) {
        free(msg->data.ptr[i].channel);
        free(msg->data.ptr[i].data.array.data.compression.algorithm);
    }
    free(msg->data.ptr);
}

void stream2_free_msg(struct stream2_msg* msg) {
    switch (msg->type) {
        case STREAM2_MSG_START:
            free_start_msg((struct stream2_start_msg*)msg);
            break;
        case STREAM2_MSG_IMAGE:
            free_image_msg((struct stream2_image_msg*)msg);
            break;
        case STREAM2_MSG_END:
            break;
    }
    free(msg->series_unique_id);
    free(msg);
}

enum stream2_result stream2_typed_array_elem_size(
        const struct stream2_typed_array* array,
        uint64_t* elem_size) {
    // https://www.rfc-editor.org/rfc/rfc8746.html#name-types-of-numbers
    if (array->tag >= 64 && array->tag <= 87) {
        const uint64_t f = (array->tag >> 4) & 1;
        const uint64_t ll = array->tag & 3;
        *elem_size = 1 << (f + ll);
        return STREAM2_OK;
    }
    return STREAM2_ERROR_NOT_IMPLEMENTED;
}

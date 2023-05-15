set(NOTE_C ${CMAKE_CURRENT_LIST_DIR}/../note-c)

idf_component_register(
    SRCS
    "gattc_demo.c"
    "note_c_hooks.c"
    "${NOTE_C}/n_atof.c"
    "${NOTE_C}/n_b64.c"
    "${NOTE_C}/n_cjson.c"
    "${NOTE_C}/n_cjson.h"
    "${NOTE_C}/n_cjson_helpers.c"
    "${NOTE_C}/n_const.c"
    "${NOTE_C}/n_ftoa.c"
    "${NOTE_C}/n_helpers.c"
    "${NOTE_C}/n_hooks.c"
    "${NOTE_C}/n_i2c.c"
    "${NOTE_C}/n_lib.h"
    "${NOTE_C}/n_md5.c"
    "${NOTE_C}/n_printf.c"
    "${NOTE_C}/n_request.c"
    "${NOTE_C}/n_serial.c"
    "${NOTE_C}/n_str.c"
    "${NOTE_C}/n_ua.c"
    INCLUDE_DIRS
    "."
    "${NOTE_C}")

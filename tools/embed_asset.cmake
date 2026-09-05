# Convert a binary/text asset into a C source file.
# Invoked with:
#   cmake -DINPUT=<path> -DOUTPUT=<path> -DSYMBOL=<name> -P embed_asset.cmake

foreach(_required INPUT OUTPUT SYMBOL)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "embed_asset.cmake: ${_required} is required")
    endif()
endforeach()

if(NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "embed_asset.cmake: input file not found: ${INPUT}")
endif()

file(READ "${INPUT}" _hex HEX)
string(LENGTH "${_hex}" _hex_len)
math(EXPR _byte_len "${_hex_len} / 2")

# Format as comma-separated C byte literals. Keeping this generated source simple
# makes the asset portable across host platforms and avoids requiring Python/xxd.
string(REGEX REPLACE "([0-9A-Fa-f][0-9A-Fa-f])" "0x\\1, " _bytes "${_hex}")

get_filename_component(_out_dir "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_out_dir}")
file(WRITE "${OUTPUT}"
    "#include <stddef.h>\n"
    "const unsigned char ${SYMBOL}[] = { ${_bytes} };\n"
    "const size_t ${SYMBOL}_len = ${_byte_len};\n"
)

# Writes the file ${INPUT} as a byte array named ${SYMBOL} into ${OUTPUT} (run with cmake -P at build time)
file(READ ${INPUT} hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
file(WRITE ${OUTPUT} "namespace ab {\nextern const unsigned char ${SYMBOL}[] = {${bytes}0};\nextern const unsigned int ${SYMBOL}Size = sizeof(${SYMBOL}) - 1;\n}\n")

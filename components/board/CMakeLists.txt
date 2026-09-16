# A leaf on purpose: it depends on nothing in this tree, so the BLE transport
# can depend on it without a cycle. Radio half only for now -- see board.h.
idf_component_register(SRCS "board.c"
                       INCLUDE_DIRS "include"
                       REQUIRES bt)

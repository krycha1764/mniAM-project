#include "amcom.h"
#include <stdint.h>

#define MNIAM_TYPE_PLAYER 0
#define MNIAM_TYPE_TRANSISTOR 1
#define MNIAM_TYPE_SPARK 2
#define MNIAM_TYPE_GLUE 3

/** AMCOM packet handler handling game packets. */
void amcomPacketHandler(const AMCOM_Packet* packet, void* userContext);


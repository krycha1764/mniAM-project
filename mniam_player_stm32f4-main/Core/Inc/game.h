#include "amcom.h"
#include <stdint.h>

#define MNIAM_TYPE_PLAYER 0
#define MNIAM_TYPE_TRANSISTOR 1
#define MNIAM_TYPE_SPARK 2
#define MNIAM_TYPE_GLUE 3

#define MNIAM_MAX_PLAYER 8
#define MNIAM_MAX_TRANSISTOR 100
#define MNIAM_MAX_SPARK 24
#define MNIAM_MAX_GLUE 8

#define MNIAM_SIZE_MAX_PLAYER 200
#define MNIAM_SIZE_TRANSISTOR 25
#define MNIAM_SIZE_SPARK 25
#define MNIAM_SIZE_GLUE 200

#define MNIAM_TRANSISTOR_WAGE 30.0
#define MNIAM_PLAYER_WAGE 30.0

/** AMCOM packet handler handling game packets. */
void amcomPacketHandler(const AMCOM_Packet* packet, void* userContext);


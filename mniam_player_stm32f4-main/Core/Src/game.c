#include "cmsis_os2.h"
#include "game.h"
#include "amcom.h"
#include "amcom_packets.h"

#include <stdio.h>
#include <string.h>

#include <FreeRTOS.h>
#include <FreeRTOS_IP.h>
#include <FreeRTOS_Sockets.h>

const char* name = "Testowanko.";
const char* helloMSG = "Bedziemy sie potykac.";
const char* endMSG = "Wszystko co dobre kiedys sie konczy.";

static AMCOM_IdentifyRequestPayload gameVersion;
static AMCOM_NewGameRequestPayload gameStats;
static AMCOM_ObjectState players[8];
static AMCOM_ObjectState transistors[100];
static AMCOM_ObjectState glue[8];
static AMCOM_ObjectState spark[24];

size_t magic_algorithm(uint8_t* buf) {

	(void)buf; // zamist tego trzeba obmyslać strategie

	AMCOM_MoveResponsePayload response;
	response.angle = 0;
	response.action = 0;
	return AMCOM_Serialize(AMCOM_MOVE_RESPONSE, &response, sizeof(response), buf);
};

/**
 * This function will be called each time a valid AMCOM packet is received
 */
void amcomPacketHandler(const AMCOM_Packet* packet, void* userContext) {
	static uint8_t buf[AMCOM_MAX_PACKET_SIZE];  // buffer used to serialize outgoing packets
	size_t toSend = 0;                          // size of the outgoing packet
	Socket_t socket = (Socket_t)userContext;    // socket used for communication with the server

	switch(packet->header.type) {
		case AMCOM_NO_PACKET: // invalid packet
			printf("Got AMCOM_NO_PACKET. NOT RESPONDING\n");
			toSend = 0;
			break;

		case AMCOM_IDENTIFY_REQUEST:
			if(packet->header.length != 4) break;
			printf("Got AMCOM_IDENTIFY_REQUEST. Responding with AMCOM_IDENTIFY_RESPONSE\n");
		memcpy(&gameVersion, packet->payload, sizeof(gameVersion));
		AMCOM_IdentifyResponsePayload identifyResponse;
		sprintf(identifyResponse.playerName, "%s", name);
		toSend = AMCOM_Serialize(AMCOM_IDENTIFY_RESPONSE, &identifyResponse, sizeof(identifyResponse), buf);
		break;

		case AMCOM_IDENTIFY_RESPONSE: // how do we get this??
			printf("Got AMCOM_IDENTIFY_RESPONSE. NOT RESPONDING\n");
			break;

		case AMCOM_NEW_GAME_REQUEST:
			if(packet->header.length != 10) break;
			printf("Got AMCOM_NEW_GAME_REQUEST. Responding with AMCOM_NEW_GAME_RESPONSE\n");
		memcpy(&gameStats, packet->payload, sizeof(gameStats));

		memset(&players, 0, sizeof(players)); // resetting all objects
		memset(&transistors, 0, sizeof(transistors));
		memset(&glue, 0, sizeof(glue));
		memset(&spark, 0, sizeof(spark));

		AMCOM_NewGameResponsePayload newgameResponse;
		sprintf(newgameResponse.helloMessage, "%s", helloMSG);
		toSend = AMCOM_Serialize(AMCOM_NEW_GAME_RESPONSE, &newgameResponse, sizeof(newgameResponse), buf);
		break;

		case AMCOM_NEW_GAME_RESPONSE: // how do we get this??
			printf("Got AMCOM_NEW_GAME_RESPONSE. NOT RESPONDING\n");
			break;

		case AMCOM_OBJECT_UPDATE_REQUEST:
			if(packet->header.length == 0) break;
			if((packet->header.length % sizeof(AMCOM_ObjectState)) != 0) break;
			printf("Got AMCOM_OBJECT_UPDATE_REQUEST.\n");
		uint8_t numPackets = packet->header.length / sizeof(AMCOM_ObjectState);
		AMCOM_ObjectState object;
		for(uint8_t n = 0; n < numPackets; n++) {
			memcpy(&object, packet->payload + n*sizeof(AMCOM_ObjectState), sizeof(AMCOM_ObjectState));
			switch(object.objectType) {
				case MNIAM_TYPE_PLAYER:
					memcpy(players + object.objectNo, &object, sizeof(AMCOM_ObjectState));
					continue;
				case MNIAM_TYPE_TRANSISTOR:
					memcpy(transistors + object.objectNo, &object, sizeof(AMCOM_ObjectState));
					continue;
				case MNIAM_TYPE_SPARK:
					memcpy(spark + object.objectNo, &object, sizeof(AMCOM_ObjectState));
					continue;
				case MNIAM_TYPE_GLUE:
					memcpy(glue + object.objectNo, &object, sizeof(AMCOM_ObjectState));
					continue;
				default: // invalid
					printf("Got invalid object type.\n");
					continue;
			}
		}
		break;

		case AMCOM_MOVE_REQUEST:
			if(packet->header.length != 4) break;
			printf("Got AMCOM_MOVE_REQUEST.\nCalculating magic algorithm... \n");
			toSend = magic_algorithm(buf);
			printf("Sending response.\n");
			break;

		case AMCOM_MOVE_RESPONSE: // how do we get this??
			printf("Got AMCOM_MOVE_RESPONSE. NOT RESPONDING\n");
			break;

		case AMCOM_GAME_OVER_REQUEST:
			printf("Got AMCOM_OBJECT_UPDATE_REQUEST.\n");
			AMCOM_GameOverResponsePayload response;
			sprintf(response.endMessage, "%s", endMSG);
			toSend = AMCOM_Serialize(AMCOM_GAME_OVER_RESPONSE, &response, sizeof(response), buf);
			break;

		case AMCOM_GAME_OVER_RESPONSE: // how do we get this??
			printf("Got AMCOM_GAME_OVER_RESPONSE. NOT RESPONDING\n");
			break;

		default: // invalid packet
			printf("Got unknowk AMCOM packet. NOT RESPONDING\n");
			toSend = 0;
			break;
	}

	// if there is something to send back - do it
	if (toSend > 0) {
		FreeRTOS_send(socket, buf, toSend, 0);
	}
}

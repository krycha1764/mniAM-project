#include "cmsis_os2.h"
#include "game.h"
#include "amcom.h"
#include "amcom_packets.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <float.h>

#include <FreeRTOS.h>
#include <FreeRTOS_IP.h>
#include <FreeRTOS_Sockets.h>


const char* name = "Testowanko.";
const char* helloMSG = "Bedziemy sie potykac.";
const char* endMSG = "Wszystko co dobre kiedys sie konczy.";

static AMCOM_IdentifyRequestPayload gameVersion;
static AMCOM_NewGameRequestPayload gameStats;
static AMCOM_ObjectState players[MNIAM_MAX_PLAYERS];
static AMCOM_ObjectState transistors[MNIAM_MAX_TRANSISTORS];
static AMCOM_ObjectState glue[MNIAM_MAX_GLUE];
static AMCOM_ObjectState spark[MNIAM_MAX_SPARKS];

size_t magic_algorithm(uint8_t* buf) {
    AMCOM_MoveResponsePayload response;
    response.angle = 0.0f;
    response.action = 0;

    // Read current player stats
    uint8_t myID = gameStats.playerNumber;
    float myX = players[myID].x;
    float myY = players[myID].y;
    int8_t myHp = players[myID].hp;

    // If the player is dead, send an empty response and skip calculations
    if (myHp <= 0) {
        return AMCOM_Serialize(AMCOM_MOVE_RESPONSE, &response, sizeof(response), buf);
    }

    // Momentum vector for smoothness
    static float momentumX = 0.0f;
    static float momentumY = 0.0f;
    
    float currentSumX = 0.0f;
    float currentSumY = 0.0f;
    
    uint8_t shouldDropSpark = 0;

    // Transistors aviability override
    int foodAvailable = 0;
    for (int i = 0; i < 100; i++) {
        if (transistors[i].hp > 0) {
            foodAvailable = 1;
            break;
        }
    }

    // Transistor vector
    for (int i = 0; i < MNIAM_MAX_TRANSISTORS; i++) {
        if (transistors[i].hp > 0) {
            float dx = transistors[i].x - myX;
            float dy = transistors[i].y - myY;
            float distSq = dx * dx + dy * dy;

            if (distSq > 1.0f) {
                currentSumX += dx * (30000.0f / distSq);
                currentSumY += dy * (30000.0f / distSq);
            }
        }
    }

    // Players vector
    // No food == whole map radius
    float preySearchRadius = foodAvailable ? 10000.0f : (gameStats.mapWidth * gameStats.mapWidth + gameStats.mapHeight * gameStats.mapHeight);
    
    float fearRadiusSq = 40000.0f; 
    
    for (int i = 0; i < MNIAM_MAX_PLAYERS; i++) {
        if (i == myID || players[i].hp <= 0) continue; 
        
        float dx = players[i].x - myX;
        float dy = players[i].y - myY;
        float distSq = dx * dx + dy * dy;
        float dist = sqrtf(distSq); 
            
        if (dist > 1.0f) {
            if (players[i].hp < myHp) {
                if (distSq < preySearchRadius) {
                    currentSumX += (dx / dist) * 200.0f;
                    currentSumY += (dy / dist) * 200.0f;
                }
            } else {
                if (distSq < fearRadiusSq) {
                    currentSumX -= (dx / dist) * 300.0f;
                    currentSumY -= (dy / dist) * 300.0f;
                    

                    // ESD
                    if (distSq < 1600.0f) { 
                        shouldDropSpark = 1; 
                    }
                }
            }
        }
    }

    // Obstacles
    // ESD avoidance and side push
    for (int i = 0; i < MNIAM_MAX_SPARKS; i++) {
        if (spark[i].hp > 0) {
            float dx = spark[i].x - myX, dy = spark[i].y - myY;
            float distSq = dx * dx + dy * dy;
            
            if (distSq < 200.0f) { 
                
                currentSumX -= dx * (4000.0f / distSq);
                currentSumY -= dy * (4000.0f / distSq);
                
                //side push 
                currentSumX += -dy * (5000.0f / distSq);
                currentSumY +=  dx * (5000.0f / distSq);
            }
        }
    }

    // Glue
    for (int i = 0; i < MNIAM_MAX_GLUE; i++) {
        if (glue[i].hp > 0) {
            float dx = glue[i].x - myX, dy = glue[i].y - myY;
            float distSq = dx * dx + dy * dy;
            if (distSq < 150.0f) {
                currentSumX -= dx * (500.0f / distSq);
                currentSumY -= dy * (500.0f / distSq);
            }
        }
    }

    //Walls avoidance
    float wallForce = 100.0f;
    if (myX < 40.0f) currentSumX += wallForce; 
    if (myX > gameStats.mapWidth - 40.0f) currentSumX -= wallForce;
    if (myY < 40.0f) currentSumY += wallForce;
    if (myY > gameStats.mapHeight - 40.0f) currentSumY -= wallForce;

    //Momentum smoothing
    momentumX = (momentumX * 0.5f) + (currentSumX * 0.5f);
    momentumY = (momentumY * 0.5f) + (currentSumY * 0.5f);

    response.angle = atan2f(momentumY, momentumX);
    response.action = shouldDropSpark;

    return AMCOM_Serialize(AMCOM_MOVE_RESPONSE, &response, sizeof(response), buf);
}

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
						break;
					case MNIAM_TYPE_TRANSISTOR:
						memcpy(transistors + object.objectNo, &object, sizeof(AMCOM_ObjectState));
						break;
					case MNIAM_TYPE_SPARK:
						memcpy(spark + object.objectNo, &object, sizeof(AMCOM_ObjectState));
						break;
					case MNIAM_TYPE_GLUE:
						memcpy(glue + object.objectNo, &object, sizeof(AMCOM_ObjectState));
						break;
					default: // invalid
						printf("Got invalid object type.\n");
						break;
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

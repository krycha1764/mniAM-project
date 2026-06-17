
#include <asm-generic/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>

#include "amcom.h"
#include "amcom_packets.h"

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

const char* name = "Testowanko.";
const char* helloMSG = "Bedziemy sie potykac.";
const char* endMSG = "Wszystko co dobre kiedys sie konczy.";

static AMCOM_IdentifyRequestPayload gameVersion;
static AMCOM_NewGameRequestPayload gameStats;
static AMCOM_ObjectState players[MNIAM_MAX_PLAYER];
static AMCOM_ObjectState transistors[MNIAM_MAX_TRANSISTOR];
static AMCOM_ObjectState glue[MNIAM_MAX_GLUE];
static AMCOM_ObjectState spark[MNIAM_MAX_SPARK];

static float calc_dist(float x1, float y1, float x2, float y2) {
  return sqrtf(powf((x2-x1), 2) + powf((y2-y1), 2));
}

static float calc_angle(float x1, float y1, float x2, float y2) {
  return atan2f((y2-y1), (x2-x1));
}

size_t magic_algorithm(uint8_t* buf) {

  AMCOM_MoveResponsePayload response;

  float myself_x = players[gameStats.playerNumber].x;
  float myself_y = players[gameStats.playerNumber].y;
  float target_x = 0.0;
  float target_y = 0.0;

  float lowest_dist_player = HUGE_VALF;
  uint8_t closest_index_player = 255;
  float lowest_dist_tran = HUGE_VALF;
  float lowest_dist_tran_non_waged = HUGE_VALF;
  uint8_t closest_index_tran = 255;

  for(uint8_t i = 0; i < MNIAM_MAX_TRANSISTOR; i++) { // najbliższy tranzystor
    if(transistors[i].objectType == MNIAM_TYPE_TRANSISTOR) {
      if(transistors[i].hp <= 0) {
        continue;
      }
      float waged_dist = calc_dist(myself_x, myself_y, transistors[i].x, transistors[i].y) - (transistors[i].hp * MNIAM_TRANSISTOR_WAGE);
      if(waged_dist < lowest_dist_tran) {
        lowest_dist_tran = waged_dist;
        closest_index_tran = i;
      }
    }
  }
  for(uint8_t i = 0; i < MNIAM_MAX_PLAYER; i++) { // najbliższy gracz o mniejszym hp
    if(players[i].objectType == MNIAM_TYPE_PLAYER) {
      if(players[i].hp >= players[gameStats.playerNumber].hp) {
        continue;
      }
      if(players[i].objectNo == gameStats.playerNumber) {
        continue;
      }
      float waged_dist = calc_dist(myself_x, myself_y, players[i].x, players[i].y) - (players[i].hp * MNIAM_PLAYER_WAGE);
      if(waged_dist < lowest_dist_player) {
        lowest_dist_player = waged_dist;
        closest_index_player = i;
      }
    }
  }

  if((lowest_dist_player > lowest_dist_tran) && (closest_index_tran != 255)) { // na co polujemy
    target_x = transistors[closest_index_tran].x;
    target_y = transistors[closest_index_tran].y;
  }else if((closest_index_player != 255)) {
    target_x = players[closest_index_tran].x;
    target_y = players[closest_index_tran].y;
  }
  response.angle = calc_angle(myself_x, myself_y, target_x, target_y);
  response.action = 0;
  return AMCOM_Serialize(AMCOM_MOVE_RESPONSE, &response, sizeof(response), buf);
};

void amcomPacketHandler(AMCOM_Packet const *packet, void *userContext) {
  uint8_t buf[AMCOM_MAX_PACKET_SIZE];              // buffer used to serialize outgoing packets
  size_t toSend = 0;                               // size of the outgoing packet
  int ConnectSocket = *((int *)userContext); // socket used for communication with the server

  switch(packet->header.type) {
  case AMCOM_NO_PACKET: // invalid packet
    printf("Got AMCOM_NO_PACKET. NOT RESPONDING\n");
    toSend = 0;
    break;

  case AMCOM_IDENTIFY_REQUEST:
    if(packet->header.length != 4)
      break;
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
    if(packet->header.length != 10)
      break;
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
    if(packet->header.length == 0)
      break;
    if((packet->header.length % sizeof(AMCOM_ObjectState)) != 0)
      break;
    printf("Got AMCOM_OBJECT_UPDATE_REQUEST.\n");
    uint8_t numPackets = packet->header.length / sizeof(AMCOM_ObjectState);
    AMCOM_ObjectState object;
    for(uint8_t n = 0; n < numPackets; n++) {
      memcpy(&object, packet->payload + n * sizeof(AMCOM_ObjectState), sizeof(AMCOM_ObjectState));
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
    if(packet->header.length != 4)
      break;
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
  if(toSend > 0) {
    int bytesSent = send(ConnectSocket, (char const *)buf, (int)toSend, 0);
    if(bytesSent == -1) {
      printf("Socket send failed with error: %s\n", strerror(errno));
      close(ConnectSocket);
      return;
    }
  }
}

#define GAME_SERVER "localhost"
#define GAME_SERVER_PORT "2001"

int main(int argc, char **argv) {
  (void)argc; // avoid unused parameter warning
  (void)argv; // avoid unused parameter warning

  printf("This is mniAM player. Let's eat some transistors! \n");

  int iResult;

  // Prepare temporary data
  int ConnectSocket = 0;
  struct addrinfo *result = NULL;
  struct addrinfo *ptr = NULL;
  struct addrinfo hints;
  char recvbuf[512];
  int recvbuflen = sizeof(recvbuf);

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = 0;
  hints.ai_flags = AI_ADDRCONFIG;

  // Resolve the game server address and port
  iResult = getaddrinfo(GAME_SERVER, GAME_SERVER_PORT, &hints, &result);
  if(iResult != 0) {
    printf("getaddrinfo failed with error: %d\n", iResult);
    return 1;
  }

  printf("Connecting to game server...\n");
  // Attempt to connect to an address until one succeeds
  for(ptr = result; ptr != NULL; ptr = ptr->ai_next) {

    // Create a SOCKET for connecting to server
    ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
    if(ConnectSocket == -1) {
      printf("Socket failed with error: %s\n", strerror(errno));
      return 1;
    }

    // Connect to server
    iResult = connect(ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
    if(iResult == SO_ERROR) {
      close(ConnectSocket);
      ConnectSocket = -1;
      continue;
    }
    break;
  }
  // Free some used resources
  freeaddrinfo(result);

  // Check if we connected to the game server
  if(ConnectSocket == -1) {
    printf("Unable to connect to the game server!\n");
    return 1;
  } else {
    printf("Connected to game server\n");
  }

  // Initialize AMCOM receiver with the packet handler and the socket as user context
  AMCOM_Receiver amReceiver;
  AMCOM_InitReceiver(&amReceiver, amcomPacketHandler, &ConnectSocket);

  // Receive until the peer closes the connection
  do {

    iResult = recv(ConnectSocket, recvbuf, recvbuflen, 0);
    if(iResult > 0) {
      AMCOM_Deserialize(&amReceiver, recvbuf, (size_t)iResult);
    } else if(iResult == 0) {
      printf("Connection closed\n");
    } else {
      printf("recv failed with error: %s\n", strerror(errno));
    }

  } while(iResult > 0);

  // No longer need the socket
  close(ConnectSocket);

  return 0;
}

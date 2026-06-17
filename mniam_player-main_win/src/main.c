#undef UNICODE
#define WIN32_LEAN_AND_MEAN

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <math.h>   // Wymagane do obliczania kątów
#include <float.h>

#include "amcom.h"
#include "amcom_packets.h"

// ==========================================
// ZMIENNE GLOBALNE STANU GRY
// ==========================================
const char* name = "Bot_Windows";
static AMCOM_IdentifyRequestPayload gameVersion;
static AMCOM_NewGameRequestPayload gameStats;
static AMCOM_ObjectState players[8];
static AMCOM_ObjectState transistors[100];
static AMCOM_ObjectState glue[8];
static AMCOM_ObjectState spark[24];

// ==========================================
// MÓZG BOTA (Pola Potencjałowe)
// ==========================================
size_t magic_algorithm(uint8_t* buf) {
    AMCOM_MoveResponsePayload response;
    response.angle = 0.0f;
    response.action = 0;

    // Retain movement vectors between frames for smooth turning
    static float momentumX = 0.0f;
    static float momentumY = 0.0f;
    
    uint8_t myID = gameStats.playerNumber;
    float myX = players[myID].x;
    float myY = players[myID].y;
    int8_t myHp = players[myID].hp;

    float currentSumX = 0.0f;
    float currentSumY = 0.0f;
    
    // Flag to determine if we should drop a defensive spark
    uint8_t shouldDropSpark = 0;

    // Check if there is any food left on the map
    int foodAvailable = 0;
    for (int i = 0; i < 100; i++) {
        if (transistors[i].hp > 0) {
            foodAvailable = 1;
            break;
        }
    }

    // --- 1. TRANSISTORS (Food Attraction) ---
    for (int i = 0; i < 100; i++) {
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

    // --- 2. PLAYERS (Hunter vs Prey Logic) ---
    // Zasięg polowania - na całą mapę jeśli nie ma jedzenia
    float preySearchRadius = foodAvailable ? 10000.0f : (gameStats.mapWidth * gameStats.mapWidth + gameStats.mapHeight * gameStats.mapHeight);
    
    // Promień strachu (ok. 200 pikseli). Ignoruj silnych, jeśli są dalej!
    float fearRadiusSq = 4000.0f; 
    
    for (int i = 0; i < 8; i++) {
        if (i == myID || players[i].hp <= 0) continue; 
        
        float dx = players[i].x - myX;
        float dy = players[i].y - myY;
        float distSq = dx * dx + dy * dy;
        float dist = sqrtf(distSq); 
            
        if (dist > 1.0f) {
            if (players[i].hp < myHp) {
                // CEL JEST SŁABSZY - ATAK
                if (distSq < preySearchRadius) {
                    currentSumX += (dx / dist) * 800.0f;
                    currentSumY += (dy / dist) * 800.0f;
                }
            } else {
                // CEL JEST SILNIEJSZY - UCIECZKA (Ale tylko, gdy wejdzie w strefę strachu!)
                if (distSq < fearRadiusSq) {
                    currentSumX -= (dx / dist) * 300.0f;
                    currentSumY -= (dy / dist) * 300.0f;
                    
                    // --- DEFENSIVE SPARK LOGIC ---
                    if (distSq < 1600.0f) { 
                        shouldDropSpark = 1; 
                    }
                }
            }
        }
    }

    // --- 3. OBSTACLES (Survival & Vortex Fields) ---
    // Sparks: Add both radial (repulsive) and tangential (vortex) forces
    for (int i = 0; i < 24; i++) {
        if (spark[i].hp > 0) {
            float dx = spark[i].x - myX, dy = spark[i].y - myY;
            float distSq = dx * dx + dy * dy;
            
            if (distSq < 200.0f) { 
                // A) Radial force (pushes straight back)
                currentSumX -= dx * (4000.0f / distSq);
                currentSumY -= dy * (4000.0f / distSq);
                
                // B) Tangential force (creates a swirl to slide around)
                // Perpendicular vector to (dx, dy) is (-dy, dx)
                currentSumX += -dy * (5000.0f / distSq);
                currentSumY +=  dx * (5000.0f / distSq);
            }
        }
    }

    // Glue: Annoyance rather than threat, react only upon contact
    for (int i = 0; i < 8; i++) {
        if (glue[i].hp > 0) {
            float dx = glue[i].x - myX, dy = glue[i].y - myY;
            float distSq = dx * dx + dy * dy;
            if (distSq < 150.0f) {
                currentSumX -= dx * (500.0f / distSq);
                currentSumY -= dy * (500.0f / distSq);
            }
        }
    }

    // --- 4. WALLS (Soft Boundaries) ---
    float wallForce = 100.0f;
    if (myX < 40.0f) currentSumX += wallForce; 
    if (myX > gameStats.mapWidth - 40.0f) currentSumX -= wallForce;
    if (myY < 40.0f) currentSumY += wallForce;
    if (myY > gameStats.mapHeight - 40.0f) currentSumY -= wallForce;

    // --- 5. MOMENTUM (Movement Smoothing) ---
    momentumX = (momentumX * 0.5f) + (currentSumX * 0.5f);
    momentumY = (momentumY * 0.5f) + (currentSumY * 0.5f);

    response.angle = atan2f(momentumY, momentumX);
    
    // Execute action (drop spark if we decided to)
    response.action = shouldDropSpark;

    return AMCOM_Serialize(AMCOM_MOVE_RESPONSE, &response, sizeof(response), buf);
}
// ==========================================
// ZAAWANSOWANA OBSŁUGA PAKIETÓW
// ==========================================
void amcomPacketHandler(AMCOM_Packet const *packet, void *userContext) {
  uint8_t buf[AMCOM_MAX_PACKET_SIZE];              
  size_t toSend = 0;                               
  SOCKET ConnectSocket = *((SOCKET *)userContext); 

  switch(packet->header.type) {
  case AMCOM_IDENTIFY_REQUEST:
    if(packet->header.length != 4) break;
    memcpy(&gameVersion, packet->payload, sizeof(gameVersion));
    AMCOM_IdentifyResponsePayload identifyResponse;
    sprintf(identifyResponse.playerName, "%s", name);
    toSend = AMCOM_Serialize(AMCOM_IDENTIFY_RESPONSE, &identifyResponse, sizeof(identifyResponse), buf);
    break;

  case AMCOM_NEW_GAME_REQUEST:
    if(packet->header.length != 10) break;
    memcpy(&gameStats, packet->payload, sizeof(gameStats));
    // Resetowanie stanu planszy przed nową grą
    memset(&players, 0, sizeof(players)); 
    memset(&transistors, 0, sizeof(transistors));
    memset(&glue, 0, sizeof(glue));
    memset(&spark, 0, sizeof(spark));

    AMCOM_NewGameResponsePayload newgameResponse;
    sprintf(newgameResponse.helloMessage, "Gotowy do testow!");
    toSend = AMCOM_Serialize(AMCOM_NEW_GAME_RESPONSE, &newgameResponse, sizeof(newgameResponse), buf);
    break;

  case AMCOM_OBJECT_UPDATE_REQUEST:
    if(packet->header.length == 0 || (packet->header.length % sizeof(AMCOM_ObjectState)) != 0) break;
    
    uint8_t numPackets = packet->header.length / sizeof(AMCOM_ObjectState);
    AMCOM_ObjectState object;
    for(uint8_t n = 0; n < numPackets; n++) {
      memcpy(&object, packet->payload + n * sizeof(AMCOM_ObjectState), sizeof(AMCOM_ObjectState));
      // Zapisujemy pozycje obiektów do odpowiednich tablic
      switch(object.objectType) {
      case 0: memcpy(players + object.objectNo, &object, sizeof(AMCOM_ObjectState)); continue;     // Player
      case 1: memcpy(transistors + object.objectNo, &object, sizeof(AMCOM_ObjectState)); continue; // Transistor
      case 2: memcpy(spark + object.objectNo, &object, sizeof(AMCOM_ObjectState)); continue;       // Spark
      case 3: memcpy(glue + object.objectNo, &object, sizeof(AMCOM_ObjectState)); continue;        // Glue
      }
    }
    break;

  case AMCOM_MOVE_REQUEST:
    if(packet->header.length != 4) break;
    // Serwer prosi o ruch, wywołujemy algorytm
    toSend = magic_algorithm(buf);
    break;

  case AMCOM_GAME_OVER_REQUEST:
    {
      AMCOM_GameOverResponsePayload response;
      sprintf(response.endMessage, "To byla dobra gra. GG WP!");
      toSend = AMCOM_Serialize(AMCOM_GAME_OVER_RESPONSE, &response, sizeof(response), buf);
    }
    break;
  }

  // Wysłanie danych do serwera (używa oryginalnych gniazd Windows)
  if(toSend > 0) {
    int bytesSent = send(ConnectSocket, (char const *)buf, (int)toSend, 0);
    if(bytesSent == SOCKET_ERROR) {
      printf("Socket send failed with error: %d\n", WSAGetLastError());
      closesocket(ConnectSocket);
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

  WSADATA wsaData;
  int iResult;

  // Initialize Winsock library (windows sockets)
  iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if(iResult != 0) {
    printf("WSAStartup failed with error: %d\n", iResult);
    return 1;
  }

  // Prepare temporary data
  SOCKET ConnectSocket = INVALID_SOCKET;
  struct addrinfo *result = NULL;
  struct addrinfo *ptr = NULL;
  struct addrinfo hints;
  char recvbuf[512];
  int recvbuflen = sizeof(recvbuf);

  ZeroMemory(&hints, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_flags = AI_PASSIVE;

  // Resolve the game server address and port
  iResult = getaddrinfo(GAME_SERVER, GAME_SERVER_PORT, &hints, &result);
  if(iResult != 0) {
    printf("getaddrinfo failed with error: %d\n", iResult);
    WSACleanup();
    return 1;
  }

  printf("Connecting to game server...\n");
  // Attempt to connect to an address until one succeeds
  for(ptr = result; ptr != NULL; ptr = ptr->ai_next) {

    // Create a SOCKET for connecting to server
    ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
    if(ConnectSocket == INVALID_SOCKET) {
      printf("Socket failed with error: %d\n", WSAGetLastError());
      WSACleanup();
      return 1;
    }

    // Connect to server
    iResult = connect(ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
    if(iResult == SOCKET_ERROR) {
      closesocket(ConnectSocket);
      ConnectSocket = INVALID_SOCKET;
      continue;
    }
    break;
  }
  // Free some used resources
  freeaddrinfo(result);

  // Check if we connected to the game server
  if(ConnectSocket == INVALID_SOCKET) {
    printf("Unable to connect to the game server!\n");
    WSACleanup();
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
      printf("recv failed with error: %d\n", WSAGetLastError());
    }

  } while(iResult > 0);

  // No longer need the socket
  closesocket(ConnectSocket);
  // Clean up
  WSACleanup();

  return 0;
}

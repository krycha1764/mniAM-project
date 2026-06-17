#include "amcom.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/// Start of packet character
uint8_t const AMCOM_SOP = 0xA1;
uint16_t const AMCOM_INITIAL_CRC = 0xFFFF;

static uint16_t AMCOM_UpdateCRC(uint8_t byte, uint16_t crc) {
  // NOLINTBEGIN(readability-magic-numbers)
  byte ^= (uint8_t)(crc & 0x00ff);
  byte ^= (uint8_t)(byte << 4);
  return ((((uint16_t)byte << 8) | (uint8_t)(crc >> 8)) ^ (uint8_t)(byte >> 4) ^ ((uint16_t)byte << 3));
  // NOLINTEND(readability-magic-numbers)
}

void AMCOM_InitReceiver(AMCOM_Receiver *receiver, AMCOM_PacketHandler packetHandlerCallback, void *userContext) {
  //if(receiver == NULL) return;
  if(packetHandlerCallback == NULL) return;

  receiver->receivedPacket.header.sop = 0;
  receiver->receivedPacket.header.type = 0;
  receiver->receivedPacket.header.length = 0;
  receiver->receivedPacket.header.crc = 0;
  for(size_t i = 0; i < AMCOM_MAX_PAYLOAD_SIZE; i++) {
    receiver->receivedPacket.payload[i] = 0;
  }
  receiver->payloadCounter = 0;
  receiver->receivedPacketState = AMCOM_PACKET_STATE_EMPTY;
  receiver->packetHandler = packetHandlerCallback;
  receiver->userContext = userContext;

}

size_t AMCOM_Serialize(uint8_t packetType, void const *payload, size_t payloadSize, uint8_t *destinationBuffer) {
  if((payload == NULL) && (payloadSize != 0)) return 0;
  if(payloadSize > AMCOM_MAX_PAYLOAD_SIZE) return 0;
  if(destinationBuffer == NULL) return 0;

  uint16_t crc = AMCOM_INITIAL_CRC;
  crc = AMCOM_UpdateCRC(packetType, crc);
  crc = AMCOM_UpdateCRC(payloadSize, crc);
  destinationBuffer[0] = AMCOM_SOP;
  destinationBuffer[1] = packetType;
  destinationBuffer[2] = payloadSize;
  for(size_t i = 0; i < payloadSize; i++) {
    destinationBuffer[i + sizeof(AMCOM_PacketHeader)] = ((uint8_t*)payload)[i];
    crc = AMCOM_UpdateCRC(((uint8_t*)payload)[i], crc);
  }
  destinationBuffer[3] = (crc & 0x00FF) >> 0;
  destinationBuffer[4] = (crc & 0xFF00) >> 8;

  return sizeof(AMCOM_PacketHeader) + payloadSize;
}

void AMCOM_Deserialize(AMCOM_Receiver *receiver, void const *data, size_t dataSize) {

  for(size_t i = 0; i < dataSize; i++) {
    uint8_t current_byte = ((uint8_t*)data)[i];

    switch(receiver->receivedPacketState) {
      case AMCOM_PACKET_STATE_EMPTY:
        if(current_byte == AMCOM_SOP) {
          receiver->receivedPacket.header.sop = current_byte;
          receiver->receivedPacketState++;
        }
        break;
      case AMCOM_PACKET_STATE_GOT_SOP:
        receiver->receivedPacket.header.type = current_byte;
        receiver->receivedPacketState++;
        break;
      case AMCOM_PACKET_STATE_GOT_TYPE:
        if(current_byte > AMCOM_MAX_PAYLOAD_SIZE) {
          receiver->receivedPacketState = AMCOM_PACKET_STATE_EMPTY;
          receiver->payloadCounter = 0;
          continue;
        }
        receiver->receivedPacket.header.length = current_byte;
        receiver->receivedPacketState++;
        break;
      case AMCOM_PACKET_STATE_GOT_LENGTH:
        receiver->receivedPacket.header.crc = current_byte;
        receiver->receivedPacketState++;
        break;
      case AMCOM_PACKET_STATE_GOT_CRC_LO:
        receiver->receivedPacket.header.crc |= current_byte << 8;
        receiver->receivedPacketState = AMCOM_PACKET_STATE_GETTING_PAYLOAD;
        if(receiver->receivedPacket.header.length == 0) {
          receiver->receivedPacketState = AMCOM_PACKET_STATE_GOT_WHOLE_PACKET;
        }
        break;
      case AMCOM_PACKET_STATE_GETTING_PAYLOAD:
        receiver->receivedPacket.payload[receiver->payloadCounter] = current_byte;
        receiver->payloadCounter++;
        if(receiver->payloadCounter == receiver->receivedPacket.header.length) {
          receiver->receivedPacketState++;
        }
        break;
      default:
    	  break;
    }
    if(receiver->receivedPacketState == AMCOM_PACKET_STATE_GOT_WHOLE_PACKET) {
      receiver->receivedPacketState = AMCOM_PACKET_STATE_EMPTY;
      receiver->payloadCounter = 0;
      if(receiver->receivedPacket.header.length > AMCOM_MAX_PAYLOAD_SIZE) {
        continue;
      }
      uint16_t crc = AMCOM_INITIAL_CRC;
      crc = AMCOM_UpdateCRC(receiver->receivedPacket.header.type, crc);
      crc = AMCOM_UpdateCRC(receiver->receivedPacket.header.length, crc);
      for(size_t i = 0; i < receiver->receivedPacket.header.length; i++) {
        crc = AMCOM_UpdateCRC(receiver->receivedPacket.payload[i], crc);
      }
      if(crc != receiver->receivedPacket.header.crc) {
        continue;
      }
      (receiver->packetHandler)(&(receiver->receivedPacket), receiver->userContext);
    }
  }

}

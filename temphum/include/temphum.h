#define PACKET_VERSION	1

static struct packet {
  uint16_t version;
  uint16_t temp;
  uint16_t hum;
  uint8_t mac[6];
  uint8_t temp_crc;
  uint8_t hum_crc;
  uint8_t status;
  uint8_t _u0[1];
  uint32_t counter;
} packet;


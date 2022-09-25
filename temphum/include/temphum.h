static struct packet {
  uint16_t temp;
  uint16_t hum;
  uint8_t temp_crc;
  uint8_t hum_crc;
  uint8_t status;
  uint8_t ident;
  uint32_t counter;
} packet;


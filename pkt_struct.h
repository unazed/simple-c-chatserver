#ifndef __CONFSERVER_H
#define __CONFSERVER_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
  char id[15];
  uint8_t code;
  char message[128];
} client_pkt_t;

typedef int sockfd_t;

enum {
  _,  /* necessary for debugging in case packets arrive as empty */
  CLIENT_IDENT,
  CLIENT_CONNECT,
  CLIENT_DISCONNECT,
  MESSAGE_TRANS,
  PRIVATE_MESSAGE,
  GENERAL_ERROR,
  CONNECT_ACK,
  INVALID_IDENT,
  INVALID_PM_IDENT
};

#endif  /* __CONFSERVER_H */

/*
 * Main client code for chatserver
 */

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <poll.h>
#include "pkt_struct.h"

#define ASSERT_NOT_REACHED assert (0);


/* structures allowing for asynchronous
 * stdin polling later */

struct pollfd stdin_poll[1] = {{
  .fd = 0,
  .events = POLLIN
  }};
char stdin_buffer[128];
uint8_t stdin_idx = 0;

sockfd_t
connect_chatserver (const char *address, unsigned short port)
/*
 * connects to the chatserver
 */
{
  sockfd_t sockfd = socket (AF_INET, SOCK_STREAM, 0);
  
  struct sockaddr_in server_addr = {
    .sin_family = AF_INET,
    .sin_port   = htons (port)
    };
  inet_pton (AF_INET, address, &server_addr.sin_addr);

  connect (sockfd, (struct sockaddr*)(&server_addr), sizeof (sockaddr_in));
  return sockfd;
}

void
send_packet (sockfd_t sockfd, char *ident, uint8_t code, const char *message)
/*
 * simple packet sending interface
 */
{
  client_pkt_t packet = {0};
  packet.code = code;
  memcpy (&packet.id, ident, 14);
  if (message != NULL)
    memcpy (&packet.message, message, 127);  /* potential issue */
  send (sockfd, &packet, sizeof (client_pkt_t), 0);
}

client_pkt_t
receive_packet (sockfd_t sockfd)
{
  client_pkt_t packet = {0};
  recv (sockfd, &packet, sizeof (client_pkt_t), 0);
  return packet;
}

int
socket_setnonblocking (sockfd_t sockfd)
/*
 * in order for user input to occur and messages to appear
 * sockets must be nonblocking upon data receiving
 */
{
  return fcntl (sockfd, F_SETFL, fcntl (sockfd, F_GETFL, 0) | O_NONBLOCK);
}

void
print_server_packet (client_pkt_t packet)
/*
 * helper method
 */
{
  printf ("|| %s: %s\n", packet.id, packet.message);
}

bool
process_server_packet (client_pkt_t packet)
/*
 * large switch-case over the protocol
 * specification
 */
{
  switch (packet.code)
    {
      case (GENERAL_ERROR):
        puts ("the server disconnected because it may be full, or its protocol "
              "is updated to a newer version");
        return false;
      case (INVALID_IDENT):
        puts ("the server disconnected because you chose an empty, or already "
              "taken identity.");
        return false;
      case (INVALID_PM_IDENT):
        puts ("your private message was unsuccessful, as the user you're trying "
              "to message doesn't exist");
        return true;
      case (PRIVATE_MESSAGE):
        printf ("PM from %s: %s\n", packet.id, packet.message);
        return true;
      case (CLIENT_CONNECT):
      case (CLIENT_DISCONNECT):
      case (CONNECT_ACK):
        print_server_packet (packet);
        return true;
      case (MESSAGE_TRANS):
        printf ("%s: %s\n", packet.id, packet.message);
        return true;
      default:
        printf ("got code=%d, message=%s\n", packet.code, packet.message);
        ASSERT_NOT_REACHED;
        return false;
    }
}

void
clear_stdin (void)
/*
 * stdin buffer is static/global, so it needs to be cleared
 * between inputs
 */
{
  memset (stdin_buffer, 0, 128);
  stdin_idx = 0;
}

bool
handle_command (char *ident, sockfd_t sockfd)
/*
 * very naive implementation of command handling,
 * only supporting /pm
 */
{
  char* command = strtok (stdin_buffer, " ");

  if (!strcmp (command, "/pm"))
    {
      char *recipient, *message;
      recipient = strtok (NULL, " ");
      if (recipient == NULL)
        {
          puts ("misformatted pm command, must have recipient");
          return true;
        }
      message = &stdin_buffer[strlen ("/pm") + strlen (recipient) + 2];
      
      client_pkt_t packet = {0};
      packet.code = PRIVATE_MESSAGE;
      memcpy (&packet.id, recipient, 14);
      memcpy (&packet.message, message, 127);

      send (sockfd, &packet, sizeof (client_pkt_t), 0);

      while (strtok (NULL, " ") != NULL);  /* clear `strtok` internal state */
    }

  clear_stdin ();
  return true;
}

bool
handle_stdin_command (char *ident, sockfd_t sockfd)
/* 
 * in case further extensions need to exist, e.g.
 * emoticon handling
 */
{
  if (stdin_buffer[0] == '/')
    return handle_command (ident, sockfd);
  stdin_buffer[stdin_idx] = 0;
  send_packet (sockfd, ident, MESSAGE_TRANS, stdin_buffer);
  clear_stdin ();
  return true;
}

bool
poll_for_stdin (char *ident, sockfd_t sockfd)
/*
 * wait every 500ms for an input on stdin,
 * read, store and then go back to the
 * event loop
 */
{
  if (poll (stdin_poll, 1, 500) <= 0)
    return false;
  read (0, &stdin_buffer[stdin_idx], 1);
  if (stdin_buffer[stdin_idx] == '\n')
    return handle_stdin_command (ident, sockfd);
  ++stdin_idx;
  if (stdin_idx >= 127)
    stdin_idx = 0;  /* start overwriting from start */
  return true;
}

void
run_chatloop_indefinitely (char *ident, sockfd_t sockfd)
{
  client_pkt_t last_message;
  
  printf ("identifying... ");
  send_packet (sockfd, ident, CLIENT_IDENT, NULL);
  last_message = receive_packet (sockfd);

  if (last_message.code != CONNECT_ACK)
    goto on_error;

  printf ("done.\nsetting server socket to non-blocking... ");
  socket_setnonblocking (sockfd);
  printf ("done.\n");

  int recv_status;

  for (;;)
    {
      recv_status = recv (sockfd, &last_message, sizeof (client_pkt_t), 0);
      if (!recv_status)
        goto on_error;
      else if (recv_status < 0 && errno != EWOULDBLOCK)
        goto on_error;
      else if (recv_status > 0)
        if (!process_server_packet (last_message))
          goto on_error;
      poll_for_stdin (ident, sockfd);
    }

on_error:
  printf ("disconnecting... ");
  close (sockfd);
  printf ("done.\n");
  return;
}

int
main (int argc, char ** argv)
{
  if (argc != 4)
    {
      printf ("%s <name {14 characters}> <server-ip> <server-port>\n", argv[0]);
      return EXIT_FAILURE;
    }

  char ident[15] = {0};
  memcpy (ident, argv[1], 14);

  const char *address = argv[2];
  unsigned short port = atoi (argv[3]);

  sockfd_t server_sockfd = connect_chatserver (address, port);

  run_chatloop_indefinitely (ident, server_sockfd);

  return EXIT_SUCCESS;
}

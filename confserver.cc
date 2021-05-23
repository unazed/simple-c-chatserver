/*
 * Server implementation for the chatserver homework
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "pkt_struct.h"
#include "client_struct.h"

#define ASSERT_NOT_REACHED assert(0);

void
printerr (const char *str)
{
  printf ("error: %s\nerrno: %s\n", str, strerror (errno));
}

sockfd_t
create_server_socket (
    const char * address, uint16_t port,
    bool reuse_addr
    )
/*
 * creates IPv4 TCP/IP socket, and binds it to
 * address:port
 */
{
  sockfd_t sockfd;
  if ( (sockfd = socket (AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) < 0)
    {
      printerr ("failed to create socket");
      return -1;
    }

  struct sockaddr_in binding_address = {
      .sin_family = AF_INET,
      .sin_port   = htons (port),
    };

  inet_pton (AF_INET, address, &binding_address.sin_addr);
  int true_ = 1;  /* necessary for `setsockopt` as silly as it looks */

  if (reuse_addr && setsockopt (sockfd, SOL_SOCKET, SO_REUSEADDR, &true_, sizeof (int)) < 0)
    {
      printerr ("failed to set SO_REUSEADDR");
      return -1;
    }
  
  if (bind (sockfd, (struct sockaddr*)(&binding_address), sizeof (struct sockaddr)) < 0)
    {
      printerr ("failed to bind address");
      return -1;
    }

  return sockfd;
}

bool
start_listening (sockfd_t sockfd, int backlog)
{
  if (listen (sockfd, backlog) < 0)
    {
      puts ("error: failed to start listening");
      return false;
    }
  return true;
}

void
broadcast_message (
    client_array_t *clients, client_t *from,
    client_pkt_t *packet
    )
/*
 * broadcast a packet to everyone except
 * the `from` client
 */
{
  for (size_t free_idx = 0; free_idx < clients->capacity; ++free_idx)
    {
      if (!clients->free_indices[free_idx])
        continue;
      else if (from == &clients->clients[free_idx])
        continue;
      send (clients->clients[free_idx].sockfd, packet, sizeof (client_pkt_t), 0);
    }
}

void
send_packet (sockfd_t sockfd, uint8_t code, const char *message)
/*
 * send a server-level packet to a client
 */
{
  client_pkt_t packet = {0};
  memcpy (&packet.id, SERVER_IDENT, strlen (SERVER_IDENT));
  if (message != NULL)
    memcpy (&packet.message, message, 127);
  packet.code = code;
  send (sockfd, &packet, sizeof (client_pkt_t), 0); 
}

void
send_private_message (client_t *from, client_t *to, const char *message)
{
  client_pkt_t packet = {0};
  memcpy (&packet.id, from->ident, 14);
  memcpy (&packet.message, message, 127);
  packet.code = PRIVATE_MESSAGE;
  send (to->sockfd, &packet, sizeof (client_pkt_t), 0);
}

void
send_connection_state (client_array_t *clients, client_t *client, bool connected)
/*
 * helper method to announce connected/disconnected clients
 */
{
  client_pkt_t packet = {0};
  
  if (connected)
    {
      packet.code = CLIENT_CONNECT;
      memcpy (packet.message, "User connected", strlen ("User connected"));
    }
  else
    {
      packet.code = CLIENT_DISCONNECT;
      memcpy (packet.message, "User disconnected", strlen ("User disconnected"));
    }

  if (client->ident != NULL)
    memcpy (packet.id, client->ident, 14);
  else
    memcpy (packet.id, "(unknown)", strlen ("(unknown)"));

  broadcast_message (clients, connected? client: NULL, &packet);
}

void
handle_client_packet (client_array_t *clients, client_t *sender, client_pkt_t packet)
/*
 * large protocol-specified switch-case handling client events
 */
{
  char *ident;
  switch (packet.code)
    {
      case (CLIENT_IDENT):
        ident = packet.id;
        if (sender->is_identified)
          {
            puts ("identified client tried to reidentify, ignoring");
            return;
          }
        else if (!strlen (ident))
          {
            printf ("Socket #%d tried to identify with empty name\n", sender->sockfd);
            send_packet (sender->sockfd, INVALID_IDENT, "Empty identity disallowed");
            client_array_remove_byref (clients, sender);
            close (sender->sockfd);
            return;
          }
        else if (client_array_contains_ident (clients, NULL, ident))
          {
            printf ("Socket #%d tried to identify with an existing name: %s\n", sender->sockfd, ident);
            send_packet (sender->sockfd, INVALID_IDENT, "Identity already exists");
            client_array_remove_byref (clients, sender);
            close (sender->sockfd);
            return;
          }
        printf ("User '%s' identified\n", ident);
        send_packet (sender->sockfd, CONNECT_ACK, "Welcome to the chatserver");
        memcpy (&sender->ident, ident, 14);
        send_connection_state (clients, sender, true);
        sender->is_identified = true;
        break;
      case (MESSAGE_TRANS):
        if (!sender->is_identified)
          {
            printf ("User '%s' tried to chat without being identified\n", sender->ident);
            send_packet (sender->sockfd, GENERAL_ERROR, "Must be identified to chat");
            client_array_remove_byref (clients, sender);
            close (sender->sockfd);
            return;
          }
        memcpy (packet.id, sender->ident, 14);
        broadcast_message (clients, sender, &packet);
        break;
      case (PRIVATE_MESSAGE):
        client_t *receiver;
        if (!sender->is_identified)
          {
            printf ("User '%s' tried to PM '%s' without being identified\n", sender->ident, packet.id);
            send_packet (sender->sockfd, GENERAL_ERROR, "Must be identified to PM");
            client_array_remove_byref (clients, sender);
            close (sender->sockfd);
            return;
          }
        else if (!client_array_contains_ident (clients, &receiver, packet.id))
          {
            printf ("User '%s' tried to PM non-existent user: '%s'\n", sender->ident, packet.id);
            send_packet (sender->sockfd, INVALID_PM_IDENT, "User doesn't exist");
            return;
          }
        send_private_message (sender, receiver, packet.message);
        break;
      default:
        puts ("unimplemented opcode sent by client");
        break;
    } 
}

void
poll_indefinitely (sockfd_t sockfd)
{
  client_array_t clients = {0};
  if (!client_array_create (&clients, 64))
    {
      puts ("error: failed to create client array");
      return;
    }

  client_pkt_t current_packet = {0}; 
  client_t current_client = {0};

  struct sockaddr_in cl_address;
  sockfd_t cl_sockfd;
  
  unsigned int unused = 0;
  ssize_t nreceived = 0;

  puts ("entering polling loop...");

  for (;;)
    {
      cl_sockfd = accept (sockfd, (struct sockaddr*)(&cl_address), &unused);
      
      /* received client connection */
      if (cl_sockfd > -1)
        {
          current_client.sockfd = cl_sockfd;
          current_client.address = cl_address;
          if (!client_array_add (&clients, &current_client))
            {
              puts ("error: failed to append new client");
              break;
            }
          fcntl (cl_sockfd, F_SETFL, fcntl (cl_sockfd, F_GETFL, 0) | O_NONBLOCK);
          /* set client socket to blocking */
        }

      for (size_t free_idx = 0; free_idx < clients.capacity; ++free_idx)
        {
          if (!clients.free_indices[free_idx])
            continue;
          nreceived = recv (clients.clients[free_idx].sockfd, &current_packet, sizeof (client_pkt_t), 0);
          if (!nreceived)
            /* indicating EOF */
            {
              send_connection_state (&clients, &clients.clients[free_idx], false);
              client_array_remove (&clients, free_idx);
              continue;
            }
          else if (nreceived == -1)
            /* indicating other recv() error */
            {
              if (errno == EWOULDBLOCK) /* blocking */
                continue;
              else if (errno == EBADF) /* bad file descriptor */
                {
                  send_connection_state (&clients, &clients.clients[free_idx], false);
                  client_array_remove (&clients, free_idx);
                  continue;
                }
              printerr ("recv() errored");
              continue;
            }
          handle_client_packet (&clients, &clients.clients[free_idx], current_packet);
        }
    }

  ASSERT_NOT_REACHED;  /* there's no reason the main loop should exit as of yet */
  client_array_free (&clients);
}

void
close_socket (sockfd_t sockfd)
{
  close (sockfd);
}

int
main (int argc, char ** argv)
{
  if (argc != 3)
    {
      printf ("%s <address> <port>\n", argv[0]);
      return EXIT_FAILURE;
    }

  const char *address = argv[1];
  uint16_t port = atoi (argv[2]);

  if (port < 30000)
    /* on `atoi` error, 0 is returned, so this is handled */
    {
      puts ("error: port must be in the range 30,000 - 65,534");
      return EXIT_FAILURE;
    }

  sockfd_t server_socket;
  if ( (server_socket = create_server_socket (
        address, port, true
        )) < 0)
      return EXIT_FAILURE;

  if (!start_listening (server_socket, 10 /* backlog */ ))
    return EXIT_FAILURE;

  poll_indefinitely (server_socket);
  close_socket (server_socket);

  return EXIT_SUCCESS;
}

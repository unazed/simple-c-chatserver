#ifndef __CLIENT_STRUCT_H
#define __CLIENT_STRUCT_H

/*
 * Client array implementation which holds
 * all clients in a tagged contiguous structure,
 * though naive and practically entirely O(n).
 * it serves its purpose
 */

#include <sys/socket.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include "pkt_struct.h"

#define DEFAULT_EXPAND_SIZE (16)
#define SERVER_IDENT        ("SERVER")

typedef struct {
  sockfd_t  sockfd;
  struct    sockaddr_in address;
  char      ident[15];
  bool      is_identified;
} client_t;

typedef struct {
  client_t  *clients;
  size_t    size;
  size_t    capacity;
  bool      *free_indices;  /* tags if an element is free to overwrite */
} client_array_t;

bool
client_array_create (client_array_t *clients, size_t capacity)
/*
 * initialize client array structure
 */
{
  clients->clients = (client_t *)calloc (capacity, sizeof (client_t));

  if (clients == NULL)
    return false;

  clients->free_indices = (bool *)calloc (capacity, sizeof (bool));  /* all initially false */
  
  if (clients->free_indices == NULL)
    {
      free (clients->clients);
      return false;
    }
  
  clients->capacity = capacity;
  
  return true;
}

bool
client_array_expand (client_array_t *clients, size_t size)
/*
 * reallocate client array by `size` elements,
 * automatically called but can be manually invoked
 */
{
  size_t new_size = clients->capacity + size;
  bool failed = (realloc (clients->clients, sizeof (client_t) * new_size) == NULL)
             || (realloc (clients->free_indices, sizeof (bool) * new_size) == NULL);
  if (!failed)
    {
      memset (&clients->free_indices[clients->capacity], 0, sizeof (bool) * size);
      clients->capacity += size;
    }
  return failed;
}

bool
client_array_add (client_array_t *clients, client_t *client)
/*
 * simple adding operation
 */
{
  ssize_t free_index = -1;
  size_t current_index;

  for (current_index = 0; current_index < clients->capacity; ++current_index)
      if (!clients->free_indices[current_index])
        {
          free_index = current_index;
          break;
        }

  /* free index not found, add space */
  if (free_index == -1)
    {
      if (!client_array_expand (clients, DEFAULT_EXPAND_SIZE))
        return false;
      free_index = current_index + 1;  /* `current_index` pointed to end of array */
    }

  memcpy (&clients->clients[free_index], client, sizeof (client_t));
  clients->free_indices[free_index] = true;
  ++clients->size;
  return true;
}

client_t*
client_array_get (client_array_t *clients, size_t idx)
/*
 * get client by their index
 */
{
  if ((idx + 1) >= clients->capacity)
    return NULL;
  else if (!clients->free_indices[idx])
    return NULL;
  return &clients->clients[idx];
}

bool
client_array_remove (client_array_t *clients, size_t idx)
/*
 * remove client by their index
 */
{
  if ((idx + 1) >= clients->capacity)
    return false;  /* out of bounds */
  else if (!clients->free_indices[idx])
    return false;  /* index empty or already removed */
  memset (&clients->clients[idx], 0, sizeof (client_t));
  clients->free_indices[idx] = false;
  --clients->size;
  return true;
}

bool
client_array_remove_byref (client_array_t *clients, client_t *client)
/*
 * remove client by their relative offset
 */
{
  size_t idx = ((size_t)client - (size_t)clients->clients) / sizeof (client_t);
  if (!clients->free_indices[idx])
    return false;  /* empty client */
  clients->free_indices[idx] = false;
  memset (client, 0, sizeof (client_t));
  --clients->size;
  return true;
} 

bool
client_array_contains_ident (client_array_t *clients, client_t **client, char *ident)
/*
 * query if client array contains an identifier
 */
{
  for (size_t free_idx = 0; free_idx < clients->capacity; ++free_idx)
    {
      if (!clients->free_indices[free_idx])
        continue;
      else if (clients->clients[free_idx].ident == NULL)
        continue;
      else if (!strcmp (clients->clients[free_idx].ident, ident))
        {
          if (client != NULL)
            *client = &clients->clients[free_idx];
          return true;
        }
    }
  if (!strcmp (ident, SERVER_IDENT))
    return true;
  return false;
}

void
client_array_free (client_array_t *clients)
{
  free (clients->free_indices);
  free (clients);
}

#endif  /* __CLIENT_STRUCT_H */

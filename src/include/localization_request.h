#ifndef LOCALIZATION_REQUEST_H
#define LOCALIZATION_REQUEST_H

#include "HAF_protocol.h"
#include "global.h"
#include "connection.h"

typedef void (*localization_request_cb_t)(void* arg);

void localization_request_init(localization_request_cb_t cb);
void handle_localization_request(ipv6_addr_t* dst);
void send_localization_request(void);
void localization_request_cb_monitored_item(void* arg);
void localization_request_cb_node(void* arg);

#endif

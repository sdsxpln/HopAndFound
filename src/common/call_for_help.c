#include "global.h"
#include "call_for_help.h"
#include "console_map.h"
#include "localization_reply.h"

static int seq_nr_send = 1;
static int seq_nr_recv = 0;

void send_call_for_help(void) {
	call_for_help_t pkg;

	pkg.type = CALL_FOR_HELP;

	if ( seq_nr_send >= 1000000 ) { //Abfrage im Empfang ber�cksichtigen
		seq_nr_send = 0;
	}
	
	pkg.seq_nr = seq_nr_send;
	seq_nr_send++;
		
	pkg.mi_id = MONITORED_ITEM_ID; //<-------------DEFINE ANLEGEN
	memcpy(&pkg.node_list, get_node_list(), MAX_NODES); //<-----------getter f�r nodelist
	udp_send(&pkg, sizeof(pkg), NULL);	
}


void forward_call_for_help(call_for_help_t* p) {
	if (p->seq_nr > seq_nr_recv){
		udp_send(p, sizeof(p), NULL);
		seq_nr_recv = p->seq_nr;
	}
}

void handle_call_for_help(call_for_help_t* p, handler_t h) {
	if ( h == NODE ) {
		forward_call_for_help(p);
	} else if ( h == MONITOR ) {
		if ( p->mi_id == MONITORED_ITEM_ID ) {
			printConsoleMap(p->node_list, MAX_NODES);
		}
	}
}

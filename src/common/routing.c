#include <stdio.h>
#include <string.h>

#include "xtimer.h"

#include "global.h"
#include "routing.h"
#include "HAF_protocol.h"
#include "connection.h"
#include "haf_queue.h"

#include "net/ipv6/addr.h"

#define TIMEOUT 9000000 //10sek.
#define EXP_TIMEOUT 30000000 //60sek.

xtimer_t timer_update;

routing_tbl_t routing_tbl[MAX_DEVICES];
update_t pkg;

/*  function _update
 *
 *	Sends the actual routing_tbl
 */
void _update(void){
#ifdef HAF_DEBUG_ROUTING
	printf("update funktion gestartet\n");
#endif

	check_exp();
	pkg.type = UPDATE;
	get_ipv6_addr(&pkg.source_adr);
	for (int i=0;i<MAX_DEVICES;i++) {
		pkg.routing_tbl[i].ip_addr = routing_tbl[i].ip_addr;
		pkg.routing_tbl[i].hops = routing_tbl[i].hops;
		pkg.routing_tbl[i].next_hop_adr = routing_tbl[i].next_hop_adr;
		pkg.routing_tbl[i].exp_time = routing_tbl[i].exp_time;
	}

#ifdef HAF_DEBUG_ROUTING
	printf("local package presend:\n");
#endif

	for(int i = 0; i < MAX_DEVICES; i++) {

#ifdef HAF_DEBUG_ROUTING
		printf("routing_tbl[%d].ip_addr: ", i); print_ipv6_string(&pkg.routing_tbl[i].ip_addr); printf("\n");
		printf("routing_tbl[%d].hops: %u\n", i, pkg.routing_tbl[i].hops);
		printf("routing_tbl[%d].next_hop_adr: ", i); print_ipv6_string(&pkg.routing_tbl[i].next_hop_adr); printf("\n");
		printf("routing_tbl[%d].exp_time: %" PRIu32 "\n", i, pkg.routing_tbl[i].exp_time);
#endif
	}
	udp_send(&pkg, sizeof(pkg), NULL);
	xtimer_set(&timer_update, TIMEOUT);
}

/*  function _routing_handler
 *
 *	Creates thread for routing
 */
void _routing_handler(void){
	if (haf_queue_enqueue((thread_function_t) _update) == -1){
#ifdef HAF_DEBUG_ROUTING
		printf("_routing_handler:: too many elements in thread queue.\n");
#endif /* HAF_DEBUG_ROUTING */
	}
}

/*  function _init
 *
 *	Initialization of the routing protocol
 */
void init(void) { //muss in der main aufgerufen werden
    timer_update.target = 0;
    timer_update.long_target = 0;
	timer_update.callback = (void*) _routing_handler;
	//xtimer_set(&timer_update, TIMEOUT);
	get_ipv6_addr(&routing_tbl[0].ip_addr);
	routing_tbl[0].hops = 0;
	routing_tbl[0].exp_time = 0;
	for (int i=1;i<MAX_DEVICES;i++) {
		ipv6_addr_set_unspecified(&routing_tbl[i].ip_addr);
		routing_tbl[i].hops = 0;
		ipv6_addr_set_unspecified(&routing_tbl[i].next_hop_adr);
		routing_tbl[i].exp_time = 0;
	}
#ifdef HAF_DEBUG_ROUTING
	puts("Init complete - start sending");
#endif
	_update();
}
/*  function handle_update
 *
 *	Checks the received routing table with the local routing table
 *	and add or delete entries
 *	If modifications are made, the routing table will be resend
 *
 *	@param p received routing table
 *	@param source_adr source adress of the received routing table
 */
void handle_update(update_t* p, ipv6_addr_t source_adr){
	static int found, change, remove = 0;

#ifdef HAF_DEBUG_ROUTING
	printf("empfagene routing tbl:\n");
	for(int i = 0; i < MAX_DEVICES; i++) {
		printf("p[%d].ip_addr: ", i); print_ipv6_string(&p->routing_tbl[i].ip_addr); printf("\n");
		printf("p[%d].hops: %u\n", i, p->routing_tbl[i].hops);
		printf("p[%d].next_hop_adr: ", i); print_ipv6_string(&p->routing_tbl[i].next_hop_adr); printf("\n");
		printf("p[%d].exp_time: %" PRIu32 "\n", i, p->routing_tbl[i].exp_time);
	}
#endif
	for (int i=0;i<MAX_DEVICES;i++) { //empfangene routing table durcharbeiten
		found = 0;
		if ( !ipv6_addr_is_unspecified(&p->routing_tbl[i].ip_addr) ) {
		for (int j=0;j<MAX_DEVICES;j++) { //eintrag in lokaler routing table suchen
			if ( ipv6_addr_equal (&p->routing_tbl[i].ip_addr, &routing_tbl[j].ip_addr) && found == 0 )  { //eintrag in lokaler routing table gefunden
				found = 1;
				if ( p->routing_tbl[i].hops == 0 ) { //eintrag direkter nachbar?

#ifdef HAF_DEBUG_ROUTING
					printf("empf. rt element %d ist ein direkter nachbar, expiration time wird erneuert in lok. rt element %d\n", i, j);
#endif
					routing_tbl[j].hops = 1;
					routing_tbl[j].next_hop_adr = routing_tbl[j].ip_addr;
					routing_tbl[j].exp_time = xtimer_now().ticks32 + EXP_TIMEOUT;
				} else { //eintrag kein direkter nachbar
					if ( p->routing_tbl[i].hops < routing_tbl[j].hops ) { //prüfe ob route kürzer ist
#ifdef HAF_DEBUG_ROUTING
						printf("empf. rt element %d ist KEIN direkter nachbar, expiration time wird erneuert in lok. rt element %d\n", i, j);
#endif
						if ( !ipv6_addr_equal(&routing_tbl[j].next_hop_adr, &source_adr) ) { //prüfe ob route über anderen nachbarn geroutet wird
#ifdef HAF_DEBUG_ROUTING
							printf("CHANGE empf. rt element %d route wird geändert in lok. rt element %d\n", i, j);
#endif
							change = 1;
							routing_tbl[j].next_hop_adr = source_adr;
						}
						routing_tbl[j].hops = p->routing_tbl[i].hops+1;
						routing_tbl[j].exp_time = xtimer_now().ticks32 + EXP_TIMEOUT;
					}
				}
			}
		}
		if ( found == 0 ) { //eintrag in lokaler routing table nicht gefunden
			for (int j=0;j<MAX_DEVICES;j++) { //suche nach freiem eintrag in lokaler routing table
				if ( ipv6_addr_is_unspecified(&routing_tbl[j].ip_addr) && found == 0) { //freier eintrag in lokaler routing table gefunden
#ifdef HAF_DEBUG_ROUTING
					printf("CHANGE empf. rt element %d eintragen in lok. rt element %d\n", i, j);
#endif
					found = 1;
					change = 1;
					routing_tbl[j].ip_addr = p->routing_tbl[i].ip_addr; //eintrag in lokale routing table eintragen
					routing_tbl[j].hops = p->routing_tbl[i].hops + 1;
					routing_tbl[j].next_hop_adr = source_adr;
					routing_tbl[j].exp_time = xtimer_now().ticks32 + EXP_TIMEOUT;
				}
			}
		}
	}
	}
	for (int j=0;j<MAX_DEVICES;j++) {
		if ( ipv6_addr_equal(&source_adr, &routing_tbl[j].next_hop_adr) ) { //prüfung auf weggefallene route beim sender
			remove = 1;
			for (int k=0;k<MAX_DEVICES;k++) { //empfangene routing table durcharbeiten
				if ( ipv6_addr_equal(&routing_tbl[j].ip_addr, &p->routing_tbl[k].ip_addr) && remove == 1 ) { //passende route gefunden
					remove = 0; //es muss nichts gelöscht werden
				}
			}
			if ( remove == 1 ) { //passende route nicht gefunden, daher route löschen

#ifdef HAF_DEBUG_ROUTING
				printf("CHANGE in empf. rt element nicht gefunden, loeschen in lok. rt element %d\n", j);
#endif
				ipv6_addr_set_unspecified(&routing_tbl[j].ip_addr);
				routing_tbl[j].hops = 0;
				ipv6_addr_set_unspecified(&routing_tbl[j].next_hop_adr);
				routing_tbl[j].exp_time = 0;
				change = 1;
			}
		}
	}
	if ( change == 1 ) {
		change = 0;
#ifdef HAF_DEBUG_ROUTING
		printf("es wurden aenderungen vorgenommen, routing table wird neu gesendet\n");
#endif
		_update();
	}
}
/*  function check_exp
 *
 *	Checks the expiration time and deletes entries if expiration time expired
 */
void check_exp(void){
	//exp time der routing table einträge prüfen
	for (int j=1;j<MAX_DEVICES;j++) { //lokale routing table durcharbeiten
		if ( routing_tbl[j].exp_time <= xtimer_now().ticks32 ) { //prüfe ob eintrag veraltet
			ipv6_addr_set_unspecified(&routing_tbl[j].ip_addr);
			routing_tbl[j].hops = 0;
			ipv6_addr_set_unspecified(&routing_tbl[j].next_hop_adr);
			routing_tbl[j].exp_time = 0;
		}
	}
}
/*  function checkroute
 *
 *	Checks, if destination address of the received call for help is in the routing table
 *
 *	@param p Received Call for Help
 */
bool checkroute(call_for_help_t* p) {
	p->ttl = p->ttl - 1;
#ifdef HAF_DEBUG_ROUTING
	printf("CALLFORHELP RECEIVED");
#endif
	for (int j=1;j<MAX_DEVICES;j++) {
		if ( ipv6_addr_equal(&p->dest_adr, &routing_tbl[j].ip_addr) ) {
#ifdef HAF_DEBUG_ROUTING
				printf("CALLFORHELP WIRD WEITERGEROUTET");
#endif
				return true;

		}
	}
#ifdef HAF_DEBUG_ROUTING
	printf("CALLFORHELP VERWORFEN, ZIELADRESSE NICHT GEFUNDEN");
#endif
	return false;
}

/*  function sendpkg
 *
 *	Sends the Call for Help to the Next Hop, if Monitor found in Routing Table, otherwise Call For Help will be dropped
 *
 *	@param p Received Call for Help
 */
void sendpkg(call_for_help_t* p) {
	int failure,sent = 0;

	for(int i = 0; i < MAX_DEVICES; i++) {
		if ( ipv6_addr_equal(&routing_tbl[i].ip_addr, &p->dest_adr) ) {
#ifdef HAF_DEBUG_ROUTING
			printf("monitor found, package sent\n");
#endif
			udp_send(p, sizeof(*p), &routing_tbl[i].next_hop_adr);
			sent = 1;
		}
		else {
			failure = 1;
		}
	}
	if (sent == 0 && failure == 1 ) {
#ifdef HAF_DEBUG_ROUTING
		printf("Monitor nicht in Routing Table gefunden, Monitor konnte nicht erreicht werden\n");
#endif
	}
}
/*  function get_route
 *
 *	Returns the number of hops through the monitor
 *
 *	@param monitor
 *
 *  @return Returns the number of hops through the monitor
 */
uint8_t get_route(ipv6_addr_t* monitor) { //gezieltes Routing ab MI
	uint8_t hops = 0;
#ifdef HAF_DEBUG_ROUTING
	printf("Route suchen fuer Monitor: "); print_ipv6_string(monitor); printf("\n");
#endif
	for(int i = 0; i < MAX_DEVICES; i++) {
		if ( ipv6_addr_equal(&routing_tbl[i].ip_addr, monitor) ) {
			hops = routing_tbl[i].hops;
#ifdef HAF_DEBUG_ROUTING
			printf("Route gefunden!\n");
#endif
		}
	}
	return hops;
}

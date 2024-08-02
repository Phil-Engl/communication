#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
#include <poll.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>

#include "rlib.h"
#include "buffer.h"



struct reliable_state {
    rel_t *next;            /* Linked list for traversing all connections */
    rel_t **prev;

    conn_t *c;          /* This is the connection object */

    /* Add your own data fields below this */
    // ...
    buffer_t* send_buffer;
    // ...
    buffer_t* rec_buffer;
    // ...
    int rcv_window;
    int send_window;
    int max_window;



    int rcv_next;
    int rcv_UNA;

    int send_next;
    int send_UNA;


    int seq_no;
    int ack_no;

    int timeout;

    int send_EOF_reached;
    int rec_EOF_reached;

    int EOF_pkt_nr;

    long wait;

    clock_t last_data_packet;

};



rel_t *rel_list;

/* Creates a new reliable protocol session, returns NULL on failure.
* ss is always NULL */
rel_t *
rel_create (conn_t *c, const struct sockaddr_storage *ss,
const struct config_common *cc)
{
    rel_t *r;

    r = xmalloc (sizeof (*r));
    memset (r, 0, sizeof (*r));

    if (!c) {
        c = conn_create (r, ss);
        if (!c) {
            free (r);
            return NULL;
        }
    }

    r->c = c;
    r->next = rel_list;
    r->prev = &rel_list;
    if (rel_list)
    rel_list->prev = &r->next;
    rel_list = r;

    /* Do any other initialization you need here... */
    // ...
    r->send_buffer = xmalloc(sizeof(buffer_t));
    r->send_buffer->head = NULL;
    // ...
    r->rec_buffer = xmalloc(sizeof(buffer_t));
    r->rec_buffer->head = NULL;
    // ...

    r->send_window = cc->window; //d höchste akzeptable packet nr am anfang isch s glieche wia max window
    r->rcv_window = cc->window;
    r->max_window = cc->window; // max window isch hald d window size diad ge kregsch

    r->send_UNA = 1;
    r->send_next = 1;

    r->seq_no = 1;
    r->ack_no = 1;

    r->rcv_next = 1;
    r->rcv_UNA = 1;

    r->timeout = cc->timeout;

    r->send_EOF_reached = 0;
    r->rec_EOF_reached = 0;

    r->EOF_pkt_nr = -1;
    r->wait = 2*cc->timeout;

    r->last_data_packet = 0;

    return r;
}

void
rel_destroy (rel_t *r)
{
    if (r->next) {
        r->next->prev = r->prev;
    }
    *r->prev = r->next;
    conn_destroy (r->c);

    /* Free any other allocated memory here */
    buffer_clear(r->send_buffer);
    free(r->send_buffer);
    buffer_clear(r->rec_buffer);
    free(r->rec_buffer);

    r->send_UNA = 1;
    r->send_next = 1;

    r->seq_no = 1;
    r->ack_no = 1;

    r->rcv_next = 1;
    r->rcv_UNA = 1;

   

    r->send_EOF_reached = 0;
    r->rec_EOF_reached = 0;

    r->EOF_pkt_nr = -1;

    r->last_data_packet = 0;
    // ...
   
    free(r);

}


// n is the expected length of pkt
void
rel_recvpkt (rel_t *r, packet_t *pkt, size_t n)
{

    int recieved_cksum = pkt->cksum;
    pkt->cksum = htons(0);

    if( n < ntohs(pkt->len) )
    {
        fprintf(stderr, "%s\n","packet smaller than expected" );
        return;
    }
    if( recieved_cksum != cksum(pkt, ntohs(pkt->len) ) )//ntohs(pkt->len))
    {
        fprintf(stderr, "checksum wrong  \n");
        return;
    }
    if(ntohs(pkt->len) > conn_bufspace(r->c))
    {
        fprintf(stderr, "buffer already full \n");
        return;
    }
    if(buffer_contains(r->rec_buffer, ntohl(pkt->seqno))) 
    {
        fprintf(stderr, "packet already in buffer \n");
       return;
    }

    if(ntohs(pkt->len) >= 12 && ntohs(pkt->len) <= 512 && ntohl(pkt->seqno) == r->rcv_next)//neuer input der output möglich macht
    {
        buffer_insert(r->rec_buffer, pkt, 0);
        rel_output(r);
        
        packet_t *ack_packet = xmalloc(sizeof(packet_t));
        ack_packet -> len = htons(8);
        ack_packet -> ackno = htonl(r->rcv_next); // des hasch gegeben: "the ackno is the next sequence number the reciever expects to get"
        ack_packet -> cksum = htons(0);

        ack_packet -> cksum = cksum(ack_packet, 8);
        conn_sendpkt(r->c, ack_packet, 8);
    }
    else if(ntohs(pkt->len) >= 12 && ntohs(pkt->len) <= 512 && ntohl(pkt->seqno) > r->rcv_next && ntohl(pkt->seqno) <= (r->rcv_next + r->max_window) ) // packet fürn buffer aba koan neua output ready => returnen
    {
        buffer_insert(r->rec_buffer, pkt, 0);
    }
    else if(ntohs(pkt->len) >= 12 && ntohs(pkt->len) <= 512 &&  ntohl(pkt->seqno) < r->rcv_next ) // resend ack packet oda duplicate
    {
        packet_t *ack_packet = xmalloc(sizeof(packet_t));
        ack_packet -> len = htons(8);
        ack_packet -> ackno = htonl(r->rcv_next); // des hasch gegeben: "the ackno is the next sequence number the reciever expects to get"
        ack_packet -> cksum = htons(0);

        ack_packet -> cksum = cksum(ack_packet, 8);
        conn_sendpkt(r->c, ack_packet, 8);
    }
    else if(ntohs(pkt->len) == 8) //ack packets => returnen
    {
        if(ntohl(pkt->ackno) > r->send_UNA ) // && buffer_contains(r->send_buffer, ntohl(pkt->ackno)-1)
        {
            r->send_UNA = ntohl(pkt->ackno);
        }
        
        if(buffer_contains(r->send_buffer, ntohl(pkt->ackno)-1))
        {
            buffer_remove(r->send_buffer, ntohl(pkt->ackno) );// des removed alle packets bis <ackno
        }
        rel_read(r);
        return;
    }
 return;
}

void
rel_read (rel_t *s)
{
    /* Your logic implementation here */
 
    if( (s->seq_no)-(s->send_UNA) < (s->max_window) )// s->max_window > buffer_size(s->send_buffer)
    {
        packet_t* p = xmalloc (sizeof (packet_t));
        int x = conn_input(s->c, p->data, 500);
     
        if(x==0) // no input available
        {
          return;
        }
        else if(x == -1 && s->EOF_pkt_nr == -1) // EOF read    
        {
            p->len = htons(12);
            s->EOF_pkt_nr = s->seq_no;
        }
        else if(x > 0) //input read
        {
            p->len =    htons(12+x);
        }
        else
        {
            return;
        }

    p->cksum =  htons(0);
    p->seqno =  htonl(s->seq_no); 
    p->cksum = cksum(p, ntohs(p->len)) ;

    s->seq_no++;

    struct timeval now; //des züg hasch alles ge ka
    gettimeofday(&now, NULL);
    long now_ms = now.tv_sec * 1000 + now.tv_usec / 1000;

    buffer_insert(s->send_buffer, p, now_ms);
    conn_sendpkt(s->c, p, ntohs(p->len));
    rel_read(s); 
    }

return;
}

void    
rel_output (rel_t *r)
{
    /* Your logic implementation here */
    buffer_node_t *curr = xmalloc(sizeof(buffer_node_t));
    curr = buffer_get_first(r->rec_buffer);
        
    while( (curr != NULL) && (ntohl(curr->packet.seqno) == r->rcv_next)  && (conn_bufspace(r->c) >= ntohs(curr->packet.len)-12 ))  
    {
        if(ntohs(curr->packet.len) > 12) 
        {
            conn_output(r->c, &curr->packet.data, ntohs(curr->packet.len)-12 );
            r->rcv_next++;
            buffer_remove(r->rec_buffer, r->rcv_next );
                
        }
        else if(ntohs(curr->packet.len)==12 && r->EOF_pkt_nr == -1) 
        {
            r->EOF_pkt_nr = ntohl(curr->packet.seqno);
            conn_output(r->c,NULL,0);
            r->rcv_next++;
            buffer_remove(r->rec_buffer, r->rcv_next);
        }
    curr = curr->next;        
    }
return;
}

void
rel_timer ()
{
    rel_t *current = rel_list;
    while (current != NULL) {
        
        buffer_node_t *curr_node = buffer_get_first(current->send_buffer);
        while(curr_node != NULL)
        {
            if( ntohl(curr_node->packet.seqno) >= current->send_UNA)
            {
                struct timeval now; //des züg hasch alles ge ka
                gettimeofday(&now, NULL);
                long now_ms = now.tv_sec * 1000 + now.tv_usec / 1000;
                long diff = now_ms - curr_node->last_retransmit;

                if(diff > (current->timeout) )
                {
                    curr_node->last_retransmit = now_ms;
                    conn_sendpkt(current->c, &curr_node->packet, ntohs(curr_node->packet.len));
                }
            
            }
        curr_node = curr_node->next;
        }
    current = rel_list->next;
    }
return;
}
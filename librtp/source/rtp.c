#include "rtp-param.h"
#include "rtp-internal.h"
#include "rtp-packet.h"

enum { 
	RTP_SENDER		= 1,	/// send RTP packet
	RTP_RECEIVER	= 2,	/// receive RTP packet
};

double rtcp_interval(int members, int senders, double rtcp_bw, int we_sent, double avg_rtcp_size, int initial);

void* rtp_create(struct rtp_event_t *handler, void* param, unsigned int ssrc, size_t frequence, size_t boundwidth)
{
	struct rtp_context *ctx;

	ctx = (struct rtp_context *)malloc(sizeof(*ctx));
	if(!ctx) return NULL;

	memset(ctx, 0, sizeof(*ctx));
	ctx->self = rtp_member_create(ssrc);
	ctx->members = rtp_member_list_create();
	ctx->senders = rtp_member_list_create();
	if(!ctx->self || !ctx->members || !ctx->senders)
	{
		rtp_destroy(ctx);
		return NULL;
	}

	rtp_member_list_add(ctx->members, ctx->self);

	memcpy(&ctx->handler, handler, sizeof(ctx->handler));
	ctx->cbparam = param;
	ctx->rtcp_bw = (size_t)(boundwidth * RTCP_BANDWIDTH_FRACTION);
	ctx->avg_rtcp_size = 0;
	ctx->frequence = frequence;
	ctx->role = RTP_RECEIVER;
	ctx->init = 1;
	return ctx;
}

int rtp_destroy(void* rtp)
{
	struct rtp_context *ctx = (struct rtp_context *)rtp;

	if(ctx->members)
		rtp_member_list_destroy(ctx->members);
	if(ctx->senders)
		rtp_member_list_destroy(ctx->senders);
	if(ctx->self)
		rtp_member_release(ctx->self);
	free(ctx);
	return 0;
}

int rtp_onsend(void* rtp, const void* data, size_t bytes)
{
//	time64_t ntp;
	rtp_packet_t pkt;
	struct rtp_context *ctx = (struct rtp_context *)rtp;

	ctx->role = RTP_SENDER;
	// don't need add self to sender list
	// rtp_member_list_add(ctx->senders, ctx->self);

	if(0 != rtp_packet_deserialize(&pkt, data, bytes))
		return -1; // packet error

	ctx->self->rtp_clock = time64_now();
	ctx->self->rtp_timestamp = pkt.rtp.timestamp; // RTP timestamp
	ctx->self->rtp_bytes += pkt.payloadlen;
	ctx->self->rtp_packets += 1;
	return 0;
}

int rtp_onreceived(void* rtp, const void* data, size_t bytes)
{
	struct rtp_context *ctx = (struct rtp_context *)rtp;
	return rtcp_input_rtp(ctx, data, bytes);
}

int rtp_onreceived_rtcp(void* rtp, const void* rtcp, size_t bytes)
{
	struct rtp_context *ctx = (struct rtp_context *)rtp;
	return rtcp_input_rtcp(ctx, rtcp, bytes);
}

size_t rtp_rtcp_report(void* rtp, void* data, size_t bytes)
{
	size_t n;
	struct rtp_context *ctx = (struct rtp_context *)rtp;

#pragma message("update we_sent flag")
	// don't send packet in 2T
	//ctx->role = RTP_RECEIVER

	if(RTP_SENDER == ctx->role)
	{
		// send RTP in 2T
		n = rtcp_sr_pack(ctx, (unsigned char*)data, bytes);
	}
	else
	{
		assert(RTP_RECEIVER == ctx->role);
		n = rtcp_rr_pack(ctx, (unsigned char*)data, bytes);
	}

	// compound RTCP Packet
	if(n < bytes)
	{
		n += rtcp_sdes_pack(ctx, (unsigned char*)data+n, bytes-n);
	}

	ctx->init = 0;
	return n;
}

size_t rtp_rtcp_bye(void* rtp, void* data, size_t bytes)
{
	struct rtp_context *ctx = (struct rtp_context *)rtp;
	return rtcp_bye_pack(ctx, (unsigned char*)data, bytes);
}

int rtp_rtcp_interval(void* rtp)
{
	double interval;
	struct rtp_context *ctx = (struct rtp_context *)rtp;
	interval = rtcp_interval(rtp_member_list_count(ctx->members),
		rtp_member_list_count(ctx->senders) + ((RTP_SENDER==ctx->role) ? 1 : 0),
		ctx->rtcp_bw, 
		(ctx->self->rtp_clock + 2*RTCP_REPORT_INTERVAL > time64_now()) ? 1 : 0,
		ctx->avg_rtcp_size,
		ctx->init);

	return (int)(interval * 1000);
}

const char* rtp_get_cname(void* rtp, unsigned int ssrc)
{
	struct rtp_member *member;
	struct rtp_context *ctx = (struct rtp_context *)rtp;
	member = rtp_member_list_find(ctx->members, ssrc);
	return member ? (char*)member->sdes[RTCP_SDES_CNAME].data : NULL;
}

const char* rtp_get_name(void* rtp, unsigned int ssrc)
{
	struct rtp_member *member;
	struct rtp_context *ctx = (struct rtp_context *)rtp;
	member = rtp_member_list_find(ctx->members, ssrc);
	return member ? (char*)member->sdes[RTCP_SDES_NAME].data : NULL;
}

int rtp_set_info(void* rtp, const char* cname, const char* name)
{
	struct rtp_context *ctx = (struct rtp_context *)rtp;
	rtp_member_setvalue(ctx->self, RTCP_SDES_CNAME, (const unsigned char*)cname, strlen(cname));
	rtp_member_setvalue(ctx->self, RTCP_SDES_NAME, (const unsigned char*)name, strlen(name));
	return 0;
}

/**
 * This file is part of mocha-ril.
 *
 * Copyright (C) 2013 Nikolay Volkov <volk204@mail.ru>
 * Copyright (C) 2013 Dominik Marszk <dmarszk@gmail.com>
 * Copyright (C) 2011-2013 Paul Kocialkowski <contact@paulk.fr>
 * Copyright (C) 2011 Denis 'GNUtoo' Carikli <GNUtoo@no-log.org>
 *
 * mocha-ril is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * mocha-ril is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with mocha-ril.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <errno.h>

#define LOG_TAG "RIL-Mocha-GPRS"
#include <utils/Log.h>
#include <cutils/properties.h>
#include <netutils/ifc.h>

#include "mocha-ril.h"
#include "util.h"
#include <proto.h>

// libnetutils missing prototype
extern int ifc_configure(const char *ifname,
	in_addr_t address,
	in_addr_t netmask,
	in_addr_t gateway,
	in_addr_t dns1,
	in_addr_t dns2);
	
struct in_addr htoina(uint32_t addr)
{
	struct in_addr ret;
	ret.s_addr = htonl(addr);
	return ret;
}

char* proto_type_to_data_call_type(uint8_t type)
{
	switch(type)
	{
		case PROTO_TYPE_IP:
			return "IP";
		case PROTO_TYPE_PPP:
			return "PPP";
		default:
			return "IP";
	}
}

uint8_t data_call_type_to_proto_type(char* type)
{
	if(!strcmp(type, "IP") || !strcmp(type, "IPV6") || !strcmp(type, "IPV4V6") )
		return PROTO_TYPE_IP;
	else if(!strcmp(type, "PPP"))
		return PROTO_TYPE_PPP;
	else
		return PROTO_TYPE_NONE;
}

int ril_gprs_connection_register(int cid)
{
	struct ril_gprs_connection *gprs_connection;
	struct list_head *list_end;
	struct list_head *list;

	gprs_connection = calloc(1, sizeof(struct ril_gprs_connection));
	if (gprs_connection == NULL)
		return -1;

	gprs_connection->cid = cid;

	list_end = ril_data.gprs_connections;
	while (list_end != NULL && list_end->next != NULL)
		list_end = list_end->next;

	list = list_head_alloc((void *) gprs_connection, list_end, NULL);

	if (ril_data.gprs_connections == NULL)
		ril_data.gprs_connections = list;

	return 0;
}

void ril_gprs_connection_unregister(struct ril_gprs_connection *gprs_connection)
{
	struct list_head *list;

	if (gprs_connection == NULL)
		return;

	list = ril_data.gprs_connections;
	while (list != NULL) {
		if (list->data == (void *) gprs_connection) {
			memset(gprs_connection, 0, sizeof(struct ril_gprs_connection));
			free(gprs_connection);

			if (list == ril_data.gprs_connections)
				ril_data.gprs_connections = list->next;

			list_head_free(list);

			break;
		}
list_continue:
		list = list->next;
	}
}

struct ril_gprs_connection *ril_gprs_connection_find_cid(int cid)
{
	struct ril_gprs_connection *gprs_connection;
	struct list_head *list;

	list = ril_data.gprs_connections;
	while (list != NULL) {
		gprs_connection = (struct ril_gprs_connection *) list->data;
		if (gprs_connection == NULL)
			goto list_continue;

		if (gprs_connection->cid == cid)
			return gprs_connection;

list_continue:
		list = list->next;
	}

	return NULL;
}

struct ril_gprs_connection *ril_gprs_connection_find_contextId(uint32_t contextId)
{
	struct ril_gprs_connection *gprs_connection;
	struct list_head *list;

	list = ril_data.gprs_connections;
	while (list != NULL) {
		gprs_connection = (struct ril_gprs_connection *) list->data;
		if (gprs_connection == NULL)
			goto list_continue;

		if (gprs_connection->contextId == contextId)
			return gprs_connection;

list_continue:
		list = list->next;
	}

	return NULL;
}

struct ril_gprs_connection *ril_gprs_connection_start(void)
{
	struct ril_gprs_connection *gprs_connection;
	struct list_head *list;
	int cid;
	int rc;
	int i;

	for (i = 0 ; i < MAX_CONNECTIONS ; i++) {
		cid = i + 1;
		list = ril_data.gprs_connections;
		while (list != NULL) {
			if (list->data == NULL)
				goto list_continue;

			gprs_connection = (struct ril_gprs_connection *) list->data;
			if (gprs_connection->cid == cid) {
				cid = 0;
				break;
				}

list_continue:
			list = list->next;
		}

		if (cid > 0)
			break;
	}

	if (cid <= 0) {
		ALOGE("Unable to find an unused cid, aborting");
		return NULL;
	}

	ALOGD("Using GPRS connection cid: %d", cid);
	rc = ril_gprs_connection_register(cid);
	if (rc < 0)
		return NULL;

	gprs_connection = ril_gprs_connection_find_cid(cid);
	asprintf(&gprs_connection->ifname, "/dev/tun%d", cid - 1);
	gprs_connection->iface = tun_alloc(gprs_connection->ifname, IFF_TUN | IFF_NO_PI);
	if(gprs_connection->iface < 0)
	{
		ALOGE("Couldn't create interface %s, errno: %d", gprs_connection->ifname, errno());
		if (gprs_connection->ifname != NULL)
			free(gprs_connection->ifname);
		ril_gprs_connection_unregister(gprs_connection);
		return NULL;
	}
	
	return gprs_connection;
}

void ril_gprs_connection_stop(struct ril_gprs_connection *gprs_connection)
{
	ALOGD("Destroying GPRS connection with cid: %d", gprs_connection->cid);
	if (gprs_connection == NULL)
		return;

	if (gprs_connection->ifname != NULL)
		free(gprs_connection->ifname);

	ril_gprs_connection_unregister(gprs_connection);
}

void ipc_proto_start_network_cnf(void* data)
{
	ALOGE("%s: Test me!", __func__);

	struct ril_gprs_connection *gprs_connection;
	protoStartNetworkCnf* netCnf = (protoStartNetworkCnf*)(data);
	RIL_Data_Call_Response_v6 *setup_data_call_response;
	char *subnet_mask = NULL;

	gprs_connection = ril_gprs_connection_find_contextId(0xFFFFFFFF);

	if (!gprs_connection) {
		ALOGE("Unable to find GPRS connection, aborting");
		return;
	}
	gprs_connection->contextId = netCnf->contextId;

	if (netCnf->error != 0)
	{
		//FIXME: add conversion for error
		ALOGE("There was an error, aborting port list complete");
		gprs_connection->fail_cause = PDP_FAIL_ERROR_UNSPECIFIED;
		ril_data.state.gprs_last_failed_cid = gprs_connection->cid;
		ril_request_complete(gprs_connection->token, RIL_E_GENERIC_FAILURE, NULL, 0);
		gprs_connection->token = 0;
		ril_gprs_connection_stop(gprs_connection); /* We can't rely on RILJ calling last_fail_cause */
		return;
	}
	
/*	//FIXME: need to find corrent subnet_mask in proto packet
	asprintf(&subnet_mask, "%d.%d.%d.%d", ((netCnf->netInfo.subnet >> 24) & 0xFF), ((netCnf->netInfo.subnet >> 16) & 0xFF), ((netCnf->netInfo.subnet >> 8) & 0xFF), ((netCnf->netInfo.subnet >> 0) & 0xFF));
	ALOGD("real subnet_mask %s", subnet_mask); */

	// FIXME: subnet isn't reliable!
	setup_data_call_response = calloc(1, sizeof(RIL_Data_Call_Response_v6));
	asprintf(&subnet_mask, "%s", "255.255.255.255");
	asprintf(&setup_data_call_response->addresses, "%s/%d",
		inet_ntoa(htoina(netCnf->netInfo.localAddr)),
		ipv4NetmaskToPrefixLength(inet_addr(subnet_mask)));
	asprintf(&setup_data_call_response->dnses, "%s %d.%d.%d.%d",
		inet_ntoa(htoina(netCnf->netInfo.dnsAddr1)),
		((netCnf->netInfo.dnsAddr2 >> 24) & 0xFF), ((netCnf->netInfo.dnsAddr2 >> 16) & 0xFF),
		((netCnf->netInfo.dnsAddr2 >> 8) & 0xFF), ((netCnf->netInfo.dnsAddr2 >> 0) & 0xFF)); //Cannot convert overlap two inet_ntoa calls
	asprintf(&setup_data_call_response->gateways, "%s", inet_ntoa(htoina(netCnf->netInfo.localAddr)));
	/*asprintf(&setup_data_call_response->gateways, "%d.%d.%d.%d", 
		((netCnf->netInfo.gatewayIp >> 24) & 0xFF), ((netCnf->netInfo.gatewayIp >> 16) & 0xFF), 
		((netCnf->netInfo.gatewayIp >> 8) & 0xFF), ((netCnf->netInfo.gatewayIp >> 0) & 0xFF));	
	*/

	setup_data_call_response->status = PDP_FAIL_NONE;
	setup_data_call_response->cid = gprs_connection->cid;
	setup_data_call_response->active = 2;
	setup_data_call_response->type = proto_type_to_data_call_type(gprs_connection->type);
	
	ALOGD("GPRS configuration: cid: %d, type: %s, iface: %s, ip: %s, gateway: %s, dnses: %s", 
		setup_data_call_response->cid, setup_data_call_response->type, 
		setup_data_call_response->ifname, setup_data_call_response->addresses, 
		setup_data_call_response->gateways, setup_data_call_response->dnses);

	ril_request_complete(gprs_connection->token, RIL_E_SUCCESS, setup_data_call_response, sizeof(RIL_Data_Call_Response_v6));
	
	if(setup_data_call_response->type)
		free(setup_data_call_response->type);
	if(setup_data_call_response->addresses)
		free(setup_data_call_response->addresses);
	if(setup_data_call_response->dnses)
		free(setup_data_call_response->dnses);
	if(setup_data_call_response->gateways)
		free(setup_data_call_response->gateways);
	if(subnet_mask)
		free(subnet_mask);
	free(setup_data_call_response);
}

void ipc_proto_receive_data_ind(void* data)
{
	ALOGE("%s: Implement me!", __func__);
}

void ril_request_setup_data_call(RIL_Token t, void *data, int length)
{
	ALOGE("%s: Test me!", __func__);

	struct ril_gprs_connection *gprs_connection = NULL;
	char *username = NULL;
	char *password = NULL;
	char *apn = NULL;
	protoStartNetwork* start_network;


	if (data == NULL || length < (int) (4 * sizeof(char *)))
		goto error;

	apn = ((char **) data)[2];
	username = ((char **) data)[3];
	password = ((char **) data)[4];

	ALOGD("Requesting data connection to APN '%s'\n", apn);

	gprs_connection = ril_gprs_connection_start();

	if (!gprs_connection) {
		ALOGE("Unable to create GPRS connection, aborting");
		ril_request_complete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
		return;
	}

	gprs_connection->type = data_call_type_to_proto_type(((char **) data)[6]);
	if(gprs_connection->type == PROTO_TYPE_NONE)
	{
		ALOGE("Unsupported data connection type %s", ((char **) data)[6]);
		ril_request_complete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
		ril_gprs_connection_stop(gprs_connection);
		return;
	}
	gprs_connection->token = t;
	gprs_connection->contextId = 0xFFFFFFFF;

	start_network = (protoStartNetwork *)malloc(sizeof(protoStartNetwork));
	memset(start_network, 0, sizeof(protoStartNetwork));

	start_network->opMode = 1;
	start_network->protoType = 1;

	unsigned int i = 0;

	if (apn != NULL)
	{
		while (i < strlen(apn))
		{
			start_network->napAddr[i] = apn[i];
			i = i + 1;
		}
	}

	start_network->preferredAccountHandle = 0x21;
	start_network->localAddr = 0xFFFFFFFF;
	start_network->dnsAddr1 = 0xFFFFFFFF;
	start_network->dnsAddr2 = 0xFFFFFFFF;
//	start_network->authType = 0x01; //PAP

	if (username != NULL)
	{
		i = 0;
		while (i < strlen(username))
		{
			start_network->userId[i] = username[i];
			i = i + 1;
		}
	}

	if (password != NULL)
	{
		i = 0;
		while (i < strlen(password))
		{
			start_network->userPasswd[i] = password[i];
			i = i + 1;
		}
	}

	proto_start_network(start_network);

	return;
error:
	ril_request_complete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

}

void ril_request_deactivate_data_call(RIL_Token t, void *data, int length)
{
	ALOGE("%s: Implement me!", __func__);
//	ril_request_complete(t, RIL_E_SUCCESS, NULL, 0);
}

void ril_request_last_data_call_fail_cause(RIL_Token t)
{
	ALOGE("%s: Test me!", __func__);
	struct ril_gprs_connection *gprs_connection;
	int last_failed_cid;
	int fail_cause;

	last_failed_cid = ril_data.state.gprs_last_failed_cid;

	if (!last_failed_cid) {
		ALOGE("No GPRS connection was reported to have failed");
		goto fail_cause_unspecified;
	}

	gprs_connection = ril_gprs_connection_find_cid(last_failed_cid);

	if (!gprs_connection) {
		ALOGE("Unable to find GPRS connection");
		goto fail_cause_unspecified;
	}

	fail_cause = gprs_connection->fail_cause;

	ril_gprs_connection_stop(gprs_connection);

	goto fail_cause_return;

fail_cause_unspecified:
	fail_cause = PDP_FAIL_ERROR_UNSPECIFIED;

fail_cause_return:
	ril_data.state.gprs_last_failed_cid = 0;

	ril_request_complete(t, RIL_E_SUCCESS, &fail_cause, sizeof(fail_cause));
}

void ril_request_data_call_list(RIL_Token t)
{
	ALOGE("%s: Implement me!", __func__);
}

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <pthread.h>
#include <unistd.h>
#include "csr_types.h"
#include "csr_type_serializer.h"
#include "csr_mesh_gateway_prim.h"
#include "csr_mesh_types.h"
#include "csr_mesh_result.h"
#include "csr_mesh_model_common.h"
#include "csr_mesh_sock.h"
#include "deserialize.h"
#include "light_client.h"
#include "light_model.h"
#include "power_client.h"
#include "power_model.h"
#include "sensor_client.h"
#include "sensor_model.h"
#include "actuator_client.h"
#include "actuator_model.h"
#include "data_client.h"
#include "data_model.h"
/*#include "csr_mesh_gw_core_api.h"*/
/*#include "csr_mesh_gw_core_callback.h"*/
/*#include "csr_mesh_gateway_prim.h"*/

#define  DEBUG   TRUE

CsrUint16    networkKey[8] = {0};
CsrUint8      nwID = 0;
CsrUint16    destDevId = 0;
CsrUint8      tid = 0;
CsrUint8      snifferEnabled = 0;
CsrUint8      repeat = 1;
CsrUint8      nwIdLen = 0;

CsrUint16    localDevId;
CsrUint8    assocStarted = 0;
CsrUint8    assocCompltd = 0;

/* CSR MESH SQL Database Keys */
#define CSR_MESH_NW_KEY_AVL_KEY  0x03
#define CSR_MESH_NW_KEY_VAL_KEY  0x04


CSRmeshResult CsrMeshCoreApiCallback(CSR_MESH_CORE_EVENT_T event, CSRmeshResult status, void *data);
void CsrMeshWaitForResponse(void);
void  InitializeModelsClient(void);
static char*  GetStatusString(CSRmeshResult status);

CSRmeshResult CsrMeshModelApiCallback(CSRMESH_MODEL_EVENT_T event_code, CSRMESH_EVENT_DATA_T* data, CsrUint16 length, void **state_data);

CSRmeshResult CsrMeshModelApiCallback(CSRMESH_MODEL_EVENT_T event_code, CSRMESH_EVENT_DATA_T* data, CsrUint16 length, void **state_data)
{
	CSRMESH_EVENT_DATA_T *ptr = (CSRMESH_EVENT_DATA_T*)data;
	CsrUint8 index;


	printf("\nMessage SeqNo=0x%x, ", data->seq_num);

	if(data->sniffed_data != NULL)
		printf("TTL=0x%x, RSSI=%d\r\n", data->sniffed_data->rx_ttl, data->sniffed_data->rx_rssi);
	switch(event_code)
	{
	case CSRMESH_DATA_BLOCK_SEND:
	{
		CSRMESH_DATA_BLOCK_SEND_T *dataBlockSend = (CSRMESH_DATA_BLOCK_SEND_T*)(ptr->data);
		/*printf(">>    DATA_BLOCK_SEND Received \n");*/
		/*printf(">>    NW_ID : 0x%x,  SRC_ID : 0x%x  , DST_ID :0x%x \n", ptr->nw_id, ptr->src_id ,ptr->dst_id);*/
		/*printf(">>    BlockLen : %x,  blockData : [", dataBlockSend->datagramOctetsLen);*/
		/*for (index = 0; index < dataBlockSend->datagramOctetsLen; index++)*/
		/*{*/
		/*	printf(" %x ", dataBlockSend->datagramOctets[index]);*/
		/*}*/
		/*printf("]\n");*/
		
		if(ptr->dst_id == 0x8ffe)
		{
			printf(">>    NW_ID : 0x%x,  SRC_ID : 0x%x\n", ptr->nw_id, ptr->src_id);
			printf("MAC Address Detected:  ");
			for (index = 1; index < 7; index++)
			{
				printf(" %02x", dataBlockSend->datagramOctets[index]);
			}
			printf("\n");
			printf("SSID: %02x", dataBlockSend->datagramOctets[7]);
			printf("\n");
		}
	}
	break;

	default:
		/*printf(">>    Model Event %x not handled\n", event_code);*/
		/*return CSR_MESH_RESULT_FAILURE;*/
		break;
	}
	return CSR_MESH_RESULT_SUCCESS;
}

void  InitializeModelsClient(void)
{
	/* Watchdog Client Init */
	WatchdogModelClientInit(CsrMeshModelApiCallback);

	/* Config Client Init */
	ConfigModelClientInit(CsrMeshModelApiCallback);

	/* Group Client Init */
	GroupModelClientInit(CsrMeshModelApiCallback);

	/* Sensor Client Init */
	SensorModelClientInit(CsrMeshModelApiCallback);

	/* Actuator Client Init */
	ActuatorModelClientInit(CsrMeshModelApiCallback);

	/* Data Client Init */
	DataModelClientInit(CsrMeshModelApiCallback);

	/* Bearer Client Init*/
	BearerModelClientInit(CsrMeshModelApiCallback);

	/* Ping Client Init */
	PingModelClientInit(CsrMeshModelApiCallback);

	/* Battery Client Init */
	BatteryModelClientInit(CsrMeshModelApiCallback);

	/* Attention Client Init */
	AttentionModelClientInit(CsrMeshModelApiCallback);

	/* Power Client Init */
	PowerModelClientInit(CsrMeshModelApiCallback);

	/* Light Client Init */
	LightModelClientInit(CsrMeshModelApiCallback);

}


void  CsrMeshWaitForResponse()
{
	/* 1. Read CSR Mesh socket */
	/* 2. Deserilaize the response */
	/* 3. Call the respective app callback */
	CsrUint8   readBuf[256];
	CsrInt16  readLen;

	printf(">>    Waiting for Response \n");
	readLen = CsrReadMesh(readBuf, 256);
	if (readLen > 0)
	{
		if (DEBUG) printf(">>    Received  %d  bytes\n",readLen);
		deserialize_msg(readBuf, readLen);
	}
	else
	{
		printf(">>    Response Not received\n");
	}
}

static char*  GetStatusString(CSRmeshResult status)
{
	switch(status)
	{
	case CSR_MESH_RESULT_SUCCESS:
		return "SUCCESS";
	case CSR_MESH_RESULT_INPROGRESS:
		return "INPROGRESS";
	case CSR_MESH_RESULT_MESH_INVALID_STATE:
		return "MESH INVALID STATE";
	case CSR_MESH_RESULT_MODEL_NOT_REGISTERED:
		return "MODEL NOT REGISTERED";
	case CSR_MESH_RESULT_MODEL_ALREADY_REGISTERD:
		return "MODEL ALREADY REGISTERD";
	case CSR_MESH_RESULT_ROLE_NOT_SUPPORTED:
		return "ROLE NOT SUPPORTED";
	case CSR_MESH_RESULT_INVALID_NWK_ID:
		return "INVALID NWK ID";
	case CSR_MESH_RESULT_EXCEED_MAX_NO_OF_NWKS:
		return "EXCEED MAX NO OF NWKS";
	case CSR_MESH_RESULT_NOT_READY:
		return "MESH NOT READY";
	case CSR_MESH_RESULT_MASP_ALREADY_ASSOCIATING:
		return "MASP ALREADY ASSOCIATING";
	case CSR_MESH_RESULT_API_NOT_SUPPORTED:
		return "API NOT SUPPORTED";
	case CSR_MESH_RESULT_TIMEOUT:
		return "TIMEOUT";
	case CSR_MESH_RESULT_INVALID_PARAMS:
		return "INVALID PARAMS";
	case CSR_MESH_RESULT_FAILURE:
		return "FAILURE";
	default:
		return "UNKNOWN RESULT";
	}
}


CSRmeshResult CsrMeshCoreApiCallback(CSR_MESH_CORE_EVENT_T eventCode, CSRmeshResult status, void *data)
{
	switch(eventCode)
	{
		/*Event received for start-mesh request*/
	case CSRMESH_START_EVENT:
	{
		printf("\n>>    CSRMESH_START_EVENT  Received Status : %s\n",GetStatusString(status));
	}
	break;

	/*Event received for stop-mesh request*/
	case CSRMESH_STOP_EVENT:
	{
		printf("\n>>    CSRMESH_STOP_EVENT  Received Status : %s\n",GetStatusString(status));
	}
	break;

	/*Event received for reset-mesh request*/
	case CSRMESH_RESET_EVENT:
	{
		printf("\n>>    CSRMESH_RESET_EVENT  Received Status : %s\n",GetStatusString(status));
	}
	break;

	/*Event received for get-device-id request*/
	case CSRMESH_GET_DEVICE_ID_EVENT:
	{
		printf("\n>>    CSRMESH_GET_DEVICE_ID_EVENT  Received Status : %s\n",GetStatusString(status));

		if(status == CSR_MESH_RESULT_SUCCESS)
		{
			printf("      Dev_ID : 0x%x\n\n",*(CsrUint16*)data);
		}
	}
	break;

	/*Event received for get device UUID request*/
	case CSRMESH_GET_DEVICE_UUID_EVENT:
	{
		printf("\n>>    CSRMESH_GET_DEVICE_UUID_EVENT  Received Status : %s\n",GetStatusString(status));

		if(status == CSR_MESH_RESULT_SUCCESS)
		{
			uint8_t  *uuidPtr = (CsrUint8*)data;
			printf("      UUID  %02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x\n\n", uuidPtr[0], uuidPtr[1], uuidPtr[2], uuidPtr[3],
			       uuidPtr[4], uuidPtr[5], uuidPtr[6], uuidPtr[7], uuidPtr[8], uuidPtr[9], uuidPtr[10], uuidPtr[11],
			       uuidPtr[12], uuidPtr[13], uuidPtr[14], uuidPtr[15]);
		}
	}
	break;

	/*Event received for associate to a network request*/
	case CSRMESH_ASSOC_TO_A_NETWORK_EVENT:
	{
		printf("\n>>    CSRMESH_ASSOC_TO_A_NETWORK_EVENT  Received Status : %s\n",GetStatusString(status));
	}
	break;

	/*Event received on association started from the configuring device*/
	case CSRMESH_ASSOC_STARTED_EVENT:
	{
		printf("\n>>    CSRMESH_ASSOC_STARTED_EVENT  Received Status : %s\n",GetStatusString(status));
		if (status == CSR_MESH_RESULT_SUCCESS)
		{
			assocStarted = 1;
		}
	}
	break;

	/*Event received on association completed */
	case CSRMESH_ASSOC_COMPLETE_EVENT:
	{
		printf("\n>>    CSRMESH_ASSOC_COMPLETE_EVENT  Received Status : %s\n",GetStatusString(status));
		if(status == CSR_MESH_RESULT_SUCCESS)
		{
			assocCompltd = 1;
			printf("      NW_ID : 0x%x\n\n",*(CsrUint8*)data);
			nwID = *(CsrUint8*)data;
		}
		else
		{
			assocCompltd =0xff;
		}
	}
	break;

	/*Event received for remove network request*/
	case CSRMESH_REMOVE_NETWORK_EVENT:
	{
		printf("\n>>   CSR_MESH_REMOVE_NETWORK_EVENT  Received Status : %s\n",GetStatusString(status));
	}
	break;

	/*Event received for set max number of network request*/
	case CSRMESH_SET_MAX_NO_OF_NETWORK_EVENT:
	{
		printf("\n>>    CSR_MESH_SET_MAX_NO_OF_NETWORK_EVENT  Received Status : %s\n",GetStatusString(status));
	}
	break;

	/*Event received for get network ID list request*/
	case CSRMESH_NETWORK_ID_LIST_EVENT:
	{
		printf("\n>>    CSRMESH_NETWORK_ID_LIST_EVENT  Received Status : %s\n",GetStatusString(status));
		if (status == CSR_MESH_RESULT_SUCCESS)
		{
			CSR_MESH_NETID_LIST_T*  list = (CSR_MESH_NETID_LIST_T*)data;
			CsrUint8 index = 0;
			nwIdLen = list->length;
			printf("      NumOfNetIds = %d\n  ", nwIdLen);
			while(index < list->length)
			{
				printf("    NetId[%d] = %x ", index, list->netIDList[index]);
				index++;
			}
			printf("\n");
		}
	}
	break;

	/*Event received for registering sniffer app callback request*/
	case CSRMESH_REGISTER_SNIFFER_EVENT:
	{
		printf("\n>>    CSRMESH_REGISTER_SNIFFER_EVENT  Received Status : %s\n",GetStatusString(status));
		/* If success, set the flag Sniffer Enabled */
		if (status == CSR_MESH_RESULT_SUCCESS)
		{
			if(snifferEnabled)
			{
				snifferEnabled = 0;
			}
			else
			{
				snifferEnabled = 1;
			}
		}
	}
	break;

	/*Event received on configuring device removing the gateway from the network*/
	case CSRMESH_CONFIG_RESET_DEVICE_EVENT:
	{
		printf("\n>>    CSRMESH_CONFIG_RESET_DEVICE_EVENT  Received Status : %s\n",GetStatusString(status));
	}
	break;

	default:
	{
		printf(">>    Unknown Event   Event Code : 0x%x\n",eventCode);
		return CSR_MESH_RESULT_FAILURE;
	}
	}
	return CSR_MESH_RESULT_SUCCESS;
}




int main(void)
{
	int ret;
	char* errMsg;

	/* Open CSR Mesh Socket */
	if ((ret = CsrMeshSockOpen(&errMsg)) < 0)
	{
		printf("Mesh Socket Open Failed : %s\n",errMsg);
		return -1;
	}

	/*CoreInit();*/
	{
		CSRmeshResult result;

		CsrMeshRegisterCallback(CsrMeshCoreApiCallback);

		result = CsrMeshStart();
		if(result != CSR_MESH_RESULT_SUCCESS)
		{
			printf("<<    Mesh Start failed!!\n");
			CsrMeshSockClose();
			return -1;
		}
		else
		{
			printf("<<    Mesh Start \n");
			CsrMeshWaitForResponse();
		}

		/* Initialize All the Supported Models Clients*/
		InitializeModelsClient();
	}

	CsrUint8  index = 0;
    CsrUint8  tempStr[10];
	while(1)
	{
		printf("\nEnter Operation ID : ");
		scanf("%s", tempStr);
		index = atoi(tempStr);
		/*StopSniffer();*/
		switch(index)
		{
		case 1:
		{
			CSRmeshResult result ;
			result = CsrMeshRegisterSniffer(FALSE);
			sleep(1);
			if(result != CSR_MESH_RESULT_SUCCESS)
			{
				printf("<<    Unregister sniffer Request  failed\n");
			}
			else
			{
				printf("\n<<    Un-register sniffer Request sent :: Waiting for Response\n");
				CsrMeshWaitForResponse();
				{
					printf(">>    Un- register sniffer Response Rxd");
				}
			}
		}
		break;

		/*StartSniffer();*/
		case 2:
		{
			CSRmeshResult result ;
			result = CsrMeshRegisterSniffer(TRUE);
			sleep(1);
			if(result != CSR_MESH_RESULT_SUCCESS)
			{
				printf("<<    sniffer Request  failed\n");

			}
			else
			{
				printf("\n<<    sniffer Request sent :: Waiting for Response\n");
				CsrMeshWaitForResponse();

				printf(">>    sniffer Response Rxd");


				if(snifferEnabled)
				{
					printf("\n Sniffer registered. Going to while loop\n");
					while(1)
					{
						CsrMeshWaitForResponse();
					}
				}
			}
		}
		break;
		default:
			printf("\error ID : ");
			break;
		}

		
	}
	return 0;
}


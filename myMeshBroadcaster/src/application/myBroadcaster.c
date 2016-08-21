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
#define  INVALID_DEVICE_ID 0xFFFF

/* Data stream send retry wait time */
#define STREAM_SEND_RETRY_TIME            (1)

/* Max number of retries */
#define MAX_SEND_RETRIES                  (3)

/* Max data per per stream send */
#define MAX_DATA_STREAM_PACKET_SIZE       (8)

typedef enum
{
	stream_send_idle = 1,     /* No stream is in progress */
	stream_start_flush_sent,  /* Stream_flush sent to start a stream */
	stream_send_in_progress,  /* stream in progress */
	stream_finish_flush_sent  /* stream_finish_flush_sent */
} stream_send_status_t;

typedef struct
{
	CsrUint16 dest_id;
	CsrUint16 sn;
	CsrUint16 last_data_len;
	stream_send_status_t status;
} STREAM_TX_T;

CsrUint16    networkKey[8] = {0};
CsrUint8      nwID = 0;
CsrUint16    destDevId = 0;
CsrUint16    destDevId_end = 0;
CsrUint8      tid = 0;
CsrUint8      repeat = 3;
CsrUint8      nwIdLen = 0;

STREAM_TX_T stream_data;

CsrUint16 tx_stream_offset=0;

char* DataMenu = "\n/*********************************/\n\
Gateway Application Menu : Data Menu\n\n\
      1   - Set Begin Remote Device ID\n\
      2   - Set End Remote Device ID\n\
      3   - Set Network Key ID\n\
      3   - Set Packet Repeat Value\n\
      5   - Set Adv Data\n\
      6   - Data Stream Send\n\
      7   - Exit\n\
/*********************************/";

CsrUint8	user_adv_data[1024];
CsrUint16	stream_user_num = 0;

CsrUint16    localDevId;
CsrUint8    assocStarted = 0;
CsrUint8    assocCompltd = 0;



/* CSR MESH SQL Database Keys */
#define CSR_MESH_NW_KEY_AVL_KEY  0x03
#define CSR_MESH_NW_KEY_VAL_KEY  0x04
CSRmeshResult CsrMeshCoreApiCallback(CSR_MESH_CORE_EVENT_T eventCode, CSRmeshResult status, void *data);

static char*  GetStatusString(CSRmeshResult status);

void  CsrMeshWaitForResponse(void);

void  InitializeModelsClient(void);

CsrUint16  ConvertStringToDeviceID(CsrUint8*  devStr);

CSRmeshResult CsrMeshModelApiCallback(CSRMESH_MODEL_EVENT_T event_code, CSRMESH_EVENT_DATA_T* data,
                                      CsrUint16 length, void **state_data);

void startStream(CsrUint16 current_id);

static void handleCSRmeshDataStreamSendCfm(CSRMESH_DATA_STREAM_RECEIVED_T *p_event);

static void sendNextPacket(void);

static void endStream(void);

static void endStream(void)
{
	printf("endStream in\n");
	CSRmeshResult result;
	if(stream_data.status == stream_send_in_progress)
	{
		stream_data.status = stream_finish_flush_sent;
		stream_data.last_data_len = 0;
	}
	result = DataStreamFlush(nwID, stream_data.dest_id, repeat, stream_data.sn);
	if(result != CSR_MESH_RESULT_SUCCESS)
	{
		printf("<<    endData stream flush failed\n");
	}
	else
	{
		printf("<<    endData stream flush sent \n");
		CsrMeshWaitForResponse();
	}
	printf("endStream out\n");
}

static void sendNextPacket(void)
{
	printf("sendNextPacket in\n");
	CSRmeshResult result;
	CsrUint16 data_pending, len;
	CSRMESH_DATA_STREAM_SEND_T send_param;

	data_pending = user_adv_data[1] - tx_stream_offset;

	if( data_pending )
	{
		len = (data_pending > MAX_DATA_STREAM_PACKET_SIZE)? MAX_DATA_STREAM_PACKET_SIZE : data_pending;

		memcpy(send_param.streamOctets, &user_adv_data[tx_stream_offset],len);
		send_param.streamSn = stream_data.sn;

		result = DataStreamSend(nwID, stream_data.dest_id, repeat, send_param.streamSn, send_param.streamOctets, len);
		stream_data.last_data_len = len;
		if(result != CSR_MESH_RESULT_SUCCESS)
		{
			printf("<<    %dData stream Send failed\n",send_param.streamSn);
			endStream();
		}
		else
		{
			printf("<<    %dData stream Send sent \n",send_param.streamSn);
			CsrMeshWaitForResponse();
		}

	}
	else
	{
		endStream();
	}
	printf("sendNextPacket out\n");
}

static void handleCSRmeshDataStreamSendCfm(CSRMESH_DATA_STREAM_RECEIVED_T *p_event)
{
	printf("handleCSRmeshDataStreamSendCfm in\n");
	tx_stream_offset += stream_data.last_data_len;
	sendNextPacket();
	printf("handleCSRmeshDataStreamSendCfm out\n");
}

void startStream(CsrUint16 current_id)
{
	CSRmeshResult result;
	stream_data.dest_id = current_id;
	stream_data.sn = 0;
	stream_data.status = stream_start_flush_sent;
	stream_data.last_data_len = 0;
	tx_stream_offset = 0;

	if (stream_data.dest_id == INVALID_DEVICE_ID)
	{
		printf("Remote Device ID is not Set\n");
		return;
	}
	result = DataStreamFlush(nwID, stream_data.dest_id, repeat, stream_data.sn);

	if(result != CSR_MESH_RESULT_SUCCESS)
	{
		printf("<<    startData stream flush failed\n");
	}
	else
	{
		printf("<<    startData stream flush sent \n");
		CsrMeshWaitForResponse();
	}
}

CSRmeshResult CsrMeshModelApiCallback(CSRMESH_MODEL_EVENT_T event_code, CSRMESH_EVENT_DATA_T* data, CsrUint16 length, void **state_data)
{
	CsrUint16 nesn;
	CSRMESH_EVENT_DATA_T *ptr = (CSRMESH_EVENT_DATA_T*)data;


	printf("\nMessage SeqNo=0x%x, ", data->seq_num);

	if(data->sniffed_data != NULL)
		printf("TTL=0x%x, RSSI=%d\r\n", data->sniffed_data->rx_ttl, data->sniffed_data->rx_rssi);

	switch(event_code)
	{
	case CSRMESH_DATA_STREAM_FLUSH:
	{
		CSRMESH_DATA_STREAM_FLUSH_T *dataStreamFlush = (CSRMESH_DATA_STREAM_FLUSH_T*)(ptr->data);
		printf(">>    DATA_STREAM_FLUSH Received \n");
		printf(">>    NW_ID : 0x%x,  SRC_ID : 0x%x  , DST_ID :0x%x \n", ptr->nw_id, ptr->src_id ,ptr->dst_id);
		printf(">>    StreamSn : 0x%x", dataStreamFlush->streamSn);


	}
	break;

	case CSRMESH_DATA_STREAM_RECEIVED:
	{
		CSRMESH_DATA_STREAM_RECEIVED_T *dataRcvd = (CSRMESH_DATA_STREAM_RECEIVED_T *)(ptr->data);
		printf(">>    CSRMESH_DATA_STREAM_RECEIVED Received \n");
		printf(">>    NW_ID : 0x%x,  SRC_ID : 0x%x  , DST_ID :0x%x  dataNesn : %x \n", ptr->nw_id, ptr->src_id ,ptr->dst_id, dataRcvd->streamNESn);

		CSRMESH_DATA_STREAM_RECEIVED_T *p_data_rcvd = (CSRMESH_DATA_STREAM_RECEIVED_T *)ptr->data;
		nesn = p_data_rcvd->streamNESn;
		printf("stream_data.sn:  %d\n", stream_data.sn);
		printf("stream_data.status:  %d\n", stream_data.status);
		
		if(stream_data.status == stream_start_flush_sent && nesn == stream_data.sn)
		{
			
			stream_data.status = stream_send_in_progress;
			handleCSRmeshDataStreamSendCfm(p_data_rcvd);
		}
		else if(stream_data.status == stream_finish_flush_sent && nesn == stream_data.sn)
		{
			stream_data.status = stream_send_idle;
			stream_data.sn = 0;
		}
		else if(stream_data.status == stream_send_in_progress && nesn == stream_data.sn + stream_data.last_data_len)
		{
			stream_data.sn += stream_data.last_data_len;
			handleCSRmeshDataStreamSendCfm(p_data_rcvd);
		}
		
	}
	break;

	default:
		printf(">>    Model Event %x not handled\n", event_code);
		return CSR_MESH_RESULT_FAILURE;
		break;
	}
	return CSR_MESH_RESULT_SUCCESS;
}

CsrUint16  ConvertStringToDeviceID(CsrUint8*  devStr)
{
	CsrUint16  devId = 0;
	CsrUint8  i = 0;
	for (i=0; i < 4; i++)
	{
		if (devStr[i] >= '0' && devStr[i] <= '9' )
			devId += ((devStr[i] - '0') << (3-i)*4);
		else if (devStr[i] >= 'A' && devStr[i] <= 'F' )
			devId += ((10 + (devStr[i] - 'A')) << (3-i)*4);
		else if (devStr[i] >= 'a' && devStr[i] <= 'f' )
			devId += ((10 + (devStr[i] - 'a')) << (3-i)*4);
		else
		{
			printf("Invalid Value entered. It should be in range of 0000 - FFFE\n");
			devId = INVALID_DEVICE_ID;
			break;
		}
	}
	return devId;

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

void  CsrMeshWaitForResponse(void)
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

	/*CSRmeshResult result;*/
	CsrUint8  index = 0;
	CsrUint8  tempStr[10];

	while(1)
	{
		/* Setting ack value to 0 */
		printf ("%s\n", DataMenu);
		printf("\nEnter Operation ID : ");
		scanf("%s",tempStr);
		index = atoi(tempStr);

		switch(index)
		{
		case 1:  /*Set Begin Remote Device ID */
		{
			destDevId = 0;
			printf("Enter Remote Device ID (in Hex form XXXX)  :  ");
			scanf("%s", tempStr);
			/* Converting Entered String to Hex Form */
			destDevId = ConvertStringToDeviceID(tempStr);
			if (destDevId != INVALID_DEVICE_ID)
				printf("Remote Device ID : 0x%x\n",destDevId);
		}
		break;
		
		case 2:  /*Set End Remote Device ID */
		{
			destDevId_end = 0;
			printf("Enter Remote Device ID (in Hex form XXXX)  :  ");
			scanf("%s", tempStr);
			/* Converting Entered String to Hex Form */
			destDevId_end = ConvertStringToDeviceID(tempStr);
			if (destDevId_end != INVALID_DEVICE_ID)
				printf("Remote Device ID : 0x%x\n",destDevId_end);
		}
		break;

		case 3: /* Set Network ID */
		{
			printf("Enter NetworkID to Use : ");
			scanf("%s", tempStr);
			nwID = atoi(tempStr);
		}
		break;

		case 4: /* Set Packet Repeat Value */
		{
			printf("Enter Packet Repeat Value : ");
			scanf("%s", tempStr);
			repeat = atoi(tempStr);
		}
		break;

		case 5:/*Set ADV data*/
		{
			fflush(stdin);
			getchar();
			printf("\nEnter text (254MAX): ");

			gets(&user_adv_data[2]);
			user_adv_data[0] = 0x03;
			user_adv_data[1] = strlen(&user_adv_data[2]) + 2;
			if(user_adv_data[1]<=254)
			{
				printf("Success，Length is %d\n",user_adv_data[1]);
				printf("%s\n", &user_adv_data[2]);
			}
			else
			{
				printf("ERROR!!The text is too long!\n");
			}
		}
		break;

		case 6:/*Data Stream Send*/
		{
			CsrUint16 count = 0;
			CsrUint16 current_id = destDevId;
			do
			{
				startStream(current_id);
				++current_id;
				++count;
				printf("COUNT:  %d\n",count);
			}
			while(current_id <= destDevId_end);
		}
		break;

		case 7: /* Exit */
		{
			printf("    !!!! closing the socket !!!!\n");
			CsrMeshSockClose();
			exit(0);
		}
		break;

		default:
		{
			/*printf("    !!! Unknown Operation !!!\n");*/
		}
		}
	}

	return 0;
}

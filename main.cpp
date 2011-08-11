//#include <Winsock2.h>
//#include <Windows.h>
//#include <stdio.h>
//
//#include "AuthServerUtil.h"
//#include "main.h"
//#include "Bignum.h"
//#include "TabulaCrypt.h"
//
//#include "GameMain.h"
#include "Global.h"

SOCKET hQueuePort;
SOCKET hGamePort;

SOCKET CreateSocket(unsigned short Port)
{
	SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	SOCKADDR_IN addr;
	memset(&addr,0,sizeof(SOCKADDR_IN));
	addr.sin_family=AF_INET;
	addr.sin_port=htons(Port);
	addr.sin_addr.s_addr=ADDR_ANY;
	if( bind(s,(SOCKADDR*)&addr,sizeof(SOCKADDR_IN)) == SOCKET_ERROR )
	{
		closesocket(s);
		return SOCKET_ERROR;
	}
	listen(s, 16);
	return s;
}

/*
	Client management for queue
*/

typedef struct
{
	SOCKET socket;
	unsigned char RecvBuffer[128];
	unsigned int RecvState;
	unsigned int RecvSize;

	unsigned int sessionId1;
	unsigned int sessionId2;

	int State;
}CLIENT_QUEUE;

typedef struct
{
	SOCKET socket;
	unsigned char RecvBuffer[512];
	unsigned int RecvState;
	unsigned int RecvSize;

	unsigned int sessionId1;
	unsigned int sessionId2;

	BIGNUM a;
	BIGNUM A;
	BIGNUM B;
	BIGNUM K;
	TABULACRYPT2 tc2;


	int State;
}CLIENT_GAMELOGIN;



#define MAX_QUEUE_CLIENTS 128
#define MAX_GAMELOGIN_CLIENTS 16

CLIENT_QUEUE Clients_Queue[MAX_QUEUE_CLIENTS];
unsigned int ClientsQueueCount = 0;
CLIENT_GAMELOGIN Clients_GameLogin[MAX_GAMELOGIN_CLIENTS];
unsigned int ClientsGameLoginCount = 0;

CLIENT_QUEUE *ClientMgr_AddToQueue(SOCKET s)
{
	if( ClientsQueueCount >= MAX_QUEUE_CLIENTS )
		return 0;
	//Reset struct
	memset((void*)&Clients_Queue[ClientsQueueCount], 0x00, sizeof(CLIENT_QUEUE));
	//Set socket
	Clients_Queue[ClientsQueueCount].socket = s;
	//Increase count and return struct
	ClientsQueueCount++;
	return &Clients_Queue[ClientsQueueCount-1];
}

CLIENT_GAMELOGIN *ClientMgr_AddToGameLogin(SOCKET s)
{
	if( ClientsGameLoginCount >= MAX_GAMELOGIN_CLIENTS )
		return 0;
	//Reset struct
	memset((void*)&Clients_GameLogin[ClientsGameLoginCount], 0x00, sizeof(CLIENT_GAMELOGIN));
	//Set socket
	Clients_GameLogin[ClientsGameLoginCount].socket = s;
	//Increase count and return struct
	ClientsGameLoginCount++;
	return &Clients_GameLogin[ClientsGameLoginCount-1];
}

int _ClientMgr_GetIndexQueue(SOCKET s)
{
	for(int i=0; i<ClientsQueueCount; i++)
		if( Clients_Queue[i].socket == s )
			return i;
	return -1;
}

int _ClientMgr_GetIndexGameLogin(SOCKET s)
{
	for(int i=0; i<ClientsGameLoginCount; i++)
		if( Clients_GameLogin[i].socket == s )
			return i;
	return -1;
}

void ClientMgr_RemoveFromQueue(SOCKET s)
{
	int Idx = _ClientMgr_GetIndexQueue(s);
	if( Idx == -1 )
		return; //No existing element
	if( (Idx+1) >= ClientsQueueCount )
	{
		//Last element - just decrease element count
		ClientsQueueCount--;
	}
	else
	{
		//Element anywhere in list, so copy last element to new space and decrease element count
		memcpy((void*)&Clients_Queue[Idx], (void*)&Clients_Queue[ClientsQueueCount-1], sizeof(CLIENT_QUEUE));
		ClientsQueueCount--;
	}
}

void ClientMgr_RemoveFromGameLogin(SOCKET s)
{
	int Idx = _ClientMgr_GetIndexGameLogin(s);
	if( Idx == -1 )
		return; //No existing element
	if( (Idx+1) >= ClientsGameLoginCount )
	{
		//Last element - just decrease element count
		ClientsGameLoginCount--;
	}
	else
	{
		//Element anywhere in list, so copy last element to new space and decrease element count
		memcpy((void*)&Clients_GameLogin[Idx], (void*)&Clients_GameLogin[ClientsQueueCount-1], sizeof(CLIENT_GAMELOGIN));
		ClientsGameLoginCount--;
	}
}


void ClientMgr_AddQueuersToFD(FD_SET *fd)
{
	for(int i=0; i<ClientsQueueCount; i++)
		FD_SET(Clients_Queue[i].socket, fd);
}

void ClientMgr_AddGameLoginsToFD(FD_SET *fd)
{
	for(int i=0; i<ClientsGameLoginCount; i++)
		FD_SET(Clients_GameLogin[i].socket, fd);
}


void ClientMgr_CallbackEventQueue(int (*cb)(CLIENT_QUEUE *cp), fd_set *fd)
{
	int idx = 0;
	while(idx < ClientsQueueCount)
	{
		if( FD_ISSET(Clients_Queue[idx].socket, fd) )
		{
			if( cb(&Clients_Queue[idx]) )
				idx++;
			else
				ClientMgr_RemoveFromQueue(Clients_Queue[idx].socket), printf("ElementQueue deleted\n"); //If cb fails - remove element
		}
		else
			idx++;
	};
}

void ClientMgr_CallbackEventGameLogin(int (*cb)(CLIENT_GAMELOGIN *cp), fd_set *fd)
{
	int idx = 0;
	while(idx < ClientsGameLoginCount)
	{
		if( FD_ISSET(Clients_GameLogin[idx].socket, fd) )
		{
			if( cb(&Clients_GameLogin[idx]) )
				idx++;
			else
				ClientMgr_RemoveFromGameLogin(Clients_GameLogin[idx].socket), printf("ElementGameLogin deleted\n"); //If cb fails - remove element
		}
		else
			idx++;
	};
}

//Prototypes
int Queuers_ReadCallback(CLIENT_QUEUE *cp);
int GameLogins_ReadCallback(CLIENT_GAMELOGIN *cp);

void SMSG_Q_SERVERKEY(CLIENT_QUEUE *cp);
int CMSG_Q_CLIENTKEY(CLIENT_QUEUE *cp, unsigned char *data, int len);
int CMSG_Q_SESSION(CLIENT_QUEUE *cp, unsigned char *data, int len);

void SMSG_GL_SERVERKEY(CLIENT_GAMELOGIN *cg);
int CMSG_GL_CLIENTKEY(CLIENT_GAMELOGIN *cg, unsigned char *data, int len);

/******** main **********/

int main()
{
	//Init Winsock
	WSADATA wsa;
	WSAStartup(MAKEWORD(2,2),&wsa);

	hQueuePort = CreateSocket(8001);
	hGamePort = CreateSocket(8102);

	printf("\xC9\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xBB\n");
	printf("\xBA     Tabula Rasa Server - Testrelease v3   \xBA\n");
	printf("\xBA     author: JH @ jhwork.net               \xBA\n");
	printf("\xBA     Experimental Branch @ InfiniteRasa    \xBA\n");
	printf("\xC8\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xBC\n");

	/*do
	{
		if( !AuthServerUtil_Register() )
		{
			printf("Auth: Register failed, retry...\n");
			Sleep(5000);
		}
		else
			break;
	}while(1);*/
	printf("Connecting to database...\n");
	dataInterface_init();
	dataInterface_registerServerForAuth();
	printf("Loading game data...\n");
	gameData_load();
	printf("running\n");
	// init other
	entityMgr_init();
	communicator_init();
	creature_init();
	mission_init();
	// init mapChannel manager
	mapChannel_init();
	// start mapChannels (all in one for now)
	int *contextIdList = (int*)malloc(sizeof(int)*mapInfoCount);
	for(int i=0; i<mapInfoCount; i++)
		contextIdList[i] = mapInfoArray[i].contextId;
	mapChannel_start(contextIdList, mapInfoCount);
	free(contextIdList);
	//Start mainserver
	DWORD p1 = 'NONE';
	CreateThread(0, 0, (LPTHREAD_START_ROUTINE)GameMain_Run, &p1, 0, 0);
	//Main socket handler loop
	FD_SET fd;
	timeval sTimeout;
	sTimeout.tv_sec = 1;
	sTimeout.tv_usec = 0;
	while(1)
	{
		FD_ZERO(&fd);
		//Add both server sockets
		FD_SET(hQueuePort, &fd);
		FD_SET(hGamePort, &fd);
		//Add all connected clients/gameservers
		ClientMgr_AddQueuersToFD(&fd);
		ClientMgr_AddGameLoginsToFD(&fd);
		//Do work select
		int r = select(0, &fd, 0, 0, &sTimeout); //Dont wait forever, we need to check Gameserver online states too
		if( r )
		{
			if( FD_ISSET(hQueuePort, &fd) )
			{
				CLIENT_QUEUE *cq = ClientMgr_AddToQueue(accept(hQueuePort, 0, 0));
				if( cq )
					SMSG_Q_SERVERKEY(cq);
			}
			if( FD_ISSET(hGamePort, &fd) )
			{
				CLIENT_GAMELOGIN *cgl = ClientMgr_AddToGameLogin(accept(hGamePort, 0, 0));
				if( cgl )
					SMSG_GL_SERVERKEY(cgl);
			}
			//Check for all receivers
			ClientMgr_CallbackEventQueue(Queuers_ReadCallback, &fd);
			ClientMgr_CallbackEventGameLogin(GameLogins_ReadCallback, &fd);
		}

		Sleep(1);
	};

	return 0;
}

//Return zero on failure
int Queuers_ReadCallback(CLIENT_QUEUE *cp)
{
	if( cp->RecvState < 4 )
	{
		int r = recv(cp->socket, (char*)cp->RecvBuffer+cp->RecvState, 4-cp->RecvState, 0);
		if( r == 0 || r == SOCKET_ERROR )
			return 0;
		cp->RecvState += r;
		if( cp->RecvState == 4 )
			cp->RecvSize = *(unsigned int*)cp->RecvBuffer + 4;
		return 1;
	}
	int r = recv(cp->socket, (char*)cp->RecvBuffer+cp->RecvState, cp->RecvSize-cp->RecvState, 0);
	if( r == 0 || r == SOCKET_ERROR )
		return 0;
	cp->RecvState += r;
	
	if( cp->RecvState == cp->RecvSize )
	{
		//Full packet received
		int res = 0;
		if( cp->State == 1 )
			res = CMSG_Q_CLIENTKEY(cp, cp->RecvBuffer, cp->RecvSize);
		else if( cp->State == 2 )
		{
			int op = cp->RecvBuffer[4];
			if( op == 7 )
				res = CMSG_Q_SESSION(cp, cp->RecvBuffer, cp->RecvSize);
			else
			{
				printf("Illegal opcode received from queue client\n");
				return 0;
			}
		}
		cp->RecvState = 0;
		return res;
	}


	return 1;
}

void SMSG_Q_SERVERKEY(CLIENT_QUEUE *cp)
{
	/*
	DWORD		PacketLen	1b 00 00 00
	DWORD		ALen		08 00 00 00
	DWORD		PrimeLen	06 00 00 00
	DWORD		GenLen		01 00 00 00
	BYTE[ALen]	Server public	50 55 42 4b 45 59 31 32 "PUBKEY12"
	BYTE[PrimeLen]	Prime		46 4f 4f 42 41 52	"FOOBAR"
	BYTE[GenLen]	Generator	41			"A"
	*/
	unsigned char ServerKeyPacket[] = {0x1b,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x06,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x50,0x55,0x42,0x4b,0x45,0x59,0x31,0x32,0x46,0x4f,0x4f,0x42,0x41,0x52,0x41};
	send(cp->socket, (char*)ServerKeyPacket, sizeof(ServerKeyPacket), 0);
	cp->State = 1;
}

int CMSG_Q_CLIENTKEY(CLIENT_QUEUE *cp, unsigned char *data, int len)
{
	/*
	DWORD		PacketLen	06 00 00 00
	BYTE[6]		Checkstring	45 4e 43 20 4f 4b "ENC OK"
	*/
	unsigned char EncOkPacket[] = {0x06,0x00,0x00,0x00,0x45,0x4e,0x43,0x20,0x4f,0x4b};
	send(cp->socket, (char*)EncOkPacket, sizeof(EncOkPacket), 0);
	//No interest for the containings of this packet, it is hardcoded anyway
	cp->State = 2;
	return 1;
}

void SMSG_Q_QUEUEPOSITION(CLIENT_QUEUE *cp, int Position)
{
	/*
	DWORD		PacketLen	09 00 00 00
	BYTE		OP		0d
	DWORD		?		00 00 00 00
	DWORD		?		05 00 00 00  
	*/
	unsigned char SendBuffer[32];
	*(unsigned int*)(SendBuffer+0) = 9;
	*(unsigned char*)(SendBuffer+4) = 0xD;
	*(unsigned int*)(SendBuffer+5) = Position;
	*(unsigned int*)(SendBuffer+9) = 0; //Since queue is not supported we also have no estimated time
	send(cp->socket, (char*)SendBuffer, 13, 0);
}

void SMSG_Q_HANDOFF(CLIENT_QUEUE *cp)
{
	/*
	DWORD		PacketLen	11 00 00 00
	BYTE		OP		0e
	DWORD		GAthSrvIP	d8 6b f8 89
	DWORD		GAthSrvPort	41 1f 00 00
	DWORD		LoginID1*	xx xx xx xx
	DWORD		LoginID2*	xx xx xx xx 
	*/
	unsigned char SendBuffer[32];
	*(unsigned int*)(SendBuffer+0) = 17;
	*(unsigned char*)(SendBuffer+4) = 0xE;
	*(unsigned int*)(SendBuffer+5) = dataInterface_getMyIP(); //IP
	*(unsigned int*)(SendBuffer+9) = 8102; //Port
	*(unsigned int*)(SendBuffer+13) = cp->sessionId1; //sessionId1
	*(unsigned int*)(SendBuffer+17) = cp->sessionId2; //sessionId2
	send(cp->socket, (char*)SendBuffer, 21, 0);
}

int CMSG_Q_SESSION(CLIENT_QUEUE *cp, unsigned char *data, int len)
{
	unsigned int sessionId1 = *(unsigned int*)(data+5);
	unsigned int sessionId2 = *(unsigned int*)(data+9);
	cp->sessionId1 = sessionId1;
	cp->sessionId2 = sessionId2;
	//Queue status - Not necessary
	SMSG_Q_QUEUEPOSITION(cp, 0);
	//Handoff instantly
	SMSG_Q_HANDOFF(cp);
	return 1;
}

/********************* GameLogin *********************/
int GameLogins_ReadCallback(CLIENT_GAMELOGIN *cg)
{
	if( cg->RecvState < 4 )
	{
		int r = recv(cg->socket, (char*)cg->RecvBuffer+cg->RecvState, 4-cg->RecvState, 0);
		if( r == 0 || r == SOCKET_ERROR )
			return 0;
		cg->RecvState += r;
		if( cg->RecvState == 4 )
			cg->RecvSize = *(unsigned int*)cg->RecvBuffer + 4;
		return 1;
	}
	int r = recv(cg->socket, (char*)cg->RecvBuffer+cg->RecvState, cg->RecvSize-cg->RecvState, 0);
	if( r == 0 || r == SOCKET_ERROR )
		return 0;
	cg->RecvState += r;
	
	if( cg->RecvState == cg->RecvSize )
	{
		//Full packet received
		int r = 0;
		if( cg->State == 1 )
			r = CMSG_GL_CLIENTKEY(cg, cg->RecvBuffer, cg->RecvSize); 
		else if( cg->State == 2 )
			__debugbreak();
		else
			return 0;
		cg->RecvState = 0;
		return r;
	}
	return 1;
}

unsigned char DH_Constant_Prime[0x40] = {
	0x98,0x0f,0x91,0xea,0xad,0xad,0x8e,0x7d,0xf9,0xec,0x43,0x1d,0xd4,0x1c,0xef,0x3f,
	0xbe,0xcf,0xd1,0xae,0xd2,0x77,0x1c,0xcf,0xf8,0x5e,0xf8,0x85,0x3e,0x2f,0x9b,0xc8,
	0x30,0x2e,0xd3,0xc4,0x7f,0xe6,0x29,0x72,0xe0,0x08,0xe9,0x32,0x53,0x97,0xdb,0x41,
	0x37,0x98,0xb3,0x8a,0xdc,0xb8,0xaf,0xd3,0x6a,0x69,0xd5,0x12,0xec,0x32,0x61,0xaf
};

unsigned char DH_Constant_Generator[1] = {5};

void DH_GeneratePrivateAndPublicA(BIGNUM *a, BIGNUM *A)
{
	BIGNUM Prime;
	BIGNUM Generator;
	Bignum_Read_BigEndian(&Prime, DH_Constant_Prime, sizeof(DH_Constant_Prime));
	Bignum_Read_BigEndian(&Generator, DH_Constant_Generator, sizeof(DH_Constant_Generator));
	//Ok we are lame we just set a, to something low
	//Bignum_SetUInt(a, 5 + (GetTickCount()&0xFFFF));
	Bignum_SetUInt(a, 19234); //Hardcoded for testing purposes
	//A = G ^ a mod P
	Bignum_ModExp(A, &Generator, a, &Prime);
}

void DH_GenerateServerK(BIGNUM *a, BIGNUM *B, BIGNUM *K)
{
	BIGNUM Prime;
	Bignum_Read_BigEndian(&Prime, DH_Constant_Prime, sizeof(DH_Constant_Prime));
	Bignum_ModExp(K, B, a, &Prime);
}

void SMSG_GL_SERVERKEY(CLIENT_GAMELOGIN *cg)
{
	DH_GeneratePrivateAndPublicA(&cg->a, &cg->A);

	//Build packet
	unsigned char Packet[0x100];
	
	*(unsigned int*)(Packet+0) = 0x8D; //Size
	*(unsigned int*)(Packet+4) = 0x40; //LenA
	*(unsigned int*)(Packet+8) = 0x40; //LenP
	*(unsigned int*)(Packet+12) = 0x01; //LenG

	unsigned char OutputA[0x40]={0};
	if( Bignum_GetLen(&cg->A) != 0x40 )
		__debugbreak();
	Bignum_Write_BigEndian(&cg->A, OutputA, 0x40); //Can have a size less than 0x40 - but we don't care

	memcpy((void*)(Packet+16), (void*)OutputA, 0x40);
	memcpy((void*)(Packet+80), (void*)DH_Constant_Prime, 0x40);
	memcpy((void*)(Packet+144), (void*)DH_Constant_Generator, 0x1);

	send(cg->socket, (char*)Packet, 145, 0);

	cg->State = 1;
}

int CMSG_GL_CLIENTKEY(CLIENT_GAMELOGIN *cg, unsigned char *data, int len)
{
	if( cg->State != 1 )
		return 0;
	//Read B len
	unsigned int BLen = *(unsigned int*)(data+4);
	if( BLen > 0x40 )
		return 1;
	Bignum_Read_BigEndian(&cg->B, (data+8), BLen);
	//Calculate K now
	DH_GenerateServerK(&cg->a, &cg->B, &cg->K);
	//Generate cryptiontable by K
	unsigned char OutputK[0x40] = {0};
	Bignum_Write_BigEndian(&cg->K, OutputK, 0x40);
	Tabula_CryptInit2(&cg->tc2, OutputK);
	//Send encrypted "ENC OK"
	unsigned char Packet_EncOK[] =
	{
		/*
		DWORD		PacketLen	08 00 00 00
		BYTE[2]		Alignbytes	02 00
		BYTE[6]		CheckString	"ENC OK"
		*/
		0x08, 0x00, 0x00, 0x00, //Header, PacketLen
		0x02, 0x00,				//Alignbytes
		'E','N','C',' ','O','K' //"ENC OK"
	};
	Tabula_Encrypt2(&cg->tc2, (unsigned int*)(Packet_EncOK+4), 8);
	send(cg->socket, (char*)Packet_EncOK, 0xC, 0);
	//Set state to 2
	cg->State = 2;
	//Pass to main game server
	GameMain_PassClient(cg->socket, &cg->tc2);
	return 0; //Drop but not disconnect client
}
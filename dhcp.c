#include<stdio.h>
#include<stdlib.h>
#include<locale.h>
#include<string.h>
#include<errno.h>
#include<sys/time.h>
#include<sys/ioctl.h>
#include<sys/socket.h>
#include<sys/types.h>
#include<fcntl.h>
#include<getopt.h>
#include<netdb.h>
#include<netinet/in.h>
#include<net/if.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<time.h>
#include<linux/if_ether.h>
#include<features.h>

#define FALSE -1
#define TRUE 0

#define MAX_DHCP_CHADDR_LEN 16
#define MAX_DHCP_SNAME_LEN 64
#define MAX_DHCP_FILE_LEN 128
#define MAX_DHCP_OPTIONS_LEN 312

struct dhcp_packet{
	uint8_t op_code; // Request or Response
	uint8_t htype; // Hardware type
	uint8_t hlen; // Hardware addr len
	uint8_t hop_count;
	uint32_t transaction_id;
	uint16_t secs;
	uint16_t flags; // Bloadcast or Unicast
	struct in_addr ciaddr; // Client ip addr 
	struct in_addr yiaddr; // Your ip addr
	struct in_addr siaddr; // DHCP Server ip addr
	struct in_addr giaddr; // Gateway ip addr
	u_char chaddr[MAX_DHCP_CHADDR_LEN]; // Client hardware addr
	char sname[MAX_DHCP_SNAME_LEN]; // Option. Server host name
	char file[MAX_DHCP_FILE_LEN]; //  Boot file name
	char options[MAX_DHCP_OPTIONS_LEN]; // Option parameters
}__attribute__((__packed__));

/*Message Operation Code*/
#define BOOTREQUEST 1
#define BOOTREPLY 2

/*DHCP Option Code*/
#define DHCP_OPTION_MESSAGE_TYPE            53
#define DHCP_OPTION_SUBNET_MASK             1
#define DHCP_OPTION_ROUTER                  3
#define DHCP_OPTION_DNS_SERVER              6
#define DHCP_OPTION_HOST_NAME               12
#define DHCP_OPTION_DOMAIN_NAME             15
#define DHCP_OPTION_BROADCAST_ADDRESS       28
#define DHCP_OPTION_REQUESTED_ADDRESS       50
#define DHCP_OPTION_LEASE_TIME              51
#define DHCP_OPTION_PARAMETER_REQUEST       55
#define DHCP_OPTION_RENEWAL_TIME            58
#define DHCP_OPTION_REBINDING_TIME          59
#define DHCP_OPTION_TFTP_SERVER_NAME        66
#define DHCP_OPTION_BOOT_FILE_NAME          67
#define DHCP_OPTION_RELAY_AGENT_INFORMATION 82

/*Option_53 = Message Type*/
#define DHCP_DISCOVER   1
#define DHCP_OFFER      2
#define DHCP_REQUEST    3
#define DHCP_DECLINE    4
#define DHCP_ACK        5
#define DHCP_NAK        6
#define DHCP_RELEASE    7
#define DHCP_INFORM     8
#define DHCP_FORCERENEW 9

#define DHCP_INFINITE_TIME 0xFFFFFFFF

/*msb 0=broadcast, 1=unicast*/
#define DHCP_BROADCAST_FLAG 32768 //0b1000000000000000

/*UDP Port Number*/
#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
 
/*DHCP_htype & DHCP_hle */
#define HTYPE_ETHER     1
#define HTYPE_ETHER_LEN 6

#define HARDWARE_ADDR "02-00-00-00-00-00"

int open_dhcp_socket(char *device);
int send_dhcp_discover(int soc, u_char *client_mac);

int open_dhcp_socket(char *device){
	struct sockaddr_in client_sin;
	struct ifreq ifr; //ifreq = InterFace Request
	int soc;
	int flag;

	/* memset = メモリに指定バイト数分の値をセット */
	memset(&client_sin, 0, sizeof(struct sockaddr_in));
	client_sin.sin_family = AF_INET; // use IPv4
	client_sin.sin_port = htons(DHCP_CLIENT_PORT);
	client_sin.sin_addr.s_addr = INADDR_ANY; // s_addr = 4-bytes integer, INADDR_ANY = 0.0.0.0


	/* 
	socket(int domain, int type, int protocol);
	type = 通信方式を指定
	protocol = ソケットによって使用される固有のプロトコルを指定
	*/
	if((soc = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))<0){
		perror("[-]Failed create socket \n"); //perror = Print ERROR message
		return FALSE;
	}

	flag=1;

	/*
	setsockopt(int socket, int level, int option_name, const void *option_value, socklen_t, option_len);
	ソケットオプションをセットする
	SOL_SOCKET = ソケットレベルでオプションを設定
	SO_REUSEADDR = ローカルアドレスの再利用の許可
	*/	
	if(setsockopt(soc, SOL_SOCKET, SO_REUSEADDR, (char *)&flag, sizeof(flag))<0){
		perror("[-]Could not set reuse address option on DHCP socket \n");
		return FALSE;
	}

	/*
	SO_BROADCAST = ブロードキャスト送信を許可
	*/
	if(setsockopt(soc, SOL_SOCKET, SO_BROADCAST, (char *)&flag, sizeof(flag))<0){
    	printf("[-]Could not set broadcast option on DHCP socket\n");
    	return FALSE;
	}

	/*
	strncpy(char *destination, const char * source, size_t num);
	*/
	strncpy(ifr.ifr_name, device, sizeof(ifr.ifr_name)-1);

	if(setsockopt(soc, SOL_SOCKET, SO_BINDTODEVICE, device, sizeof(device))>0){
		printf("[-]Could not bind socket to device \n");
		return FALSE;
	}

	/*
	int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
	addrで指定されたアドレスを割り当てる
	addrlenのはaddrが指すアドレス構造体のサイズをバイト単位で指定する
	*/
	if(bind(soc, (struct sockaddr *)&client_sin, sizeof(client_sin))<0){
		perror("[-]Failed to bind \n");
    	return FALSE;
	}

	return soc;
}

int open_dhcp_socket_for_recv(){
	struct sockaddr_in client_sin;
	int soc, flag;

	memset(&client_sin, 0, sizeof(struct sockaddr_in));
	client_sin.sin_family = AF_INET;
	client_sin.sin_port = htons(DHCP_CLIENT_PORT);
	client_sin.sin_addr.s_addr = INADDR_ANY;

	if((soc = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))<0){
		perror("[-]Failed create socket for recv\n");
		return FALSE;
	}

	flag = 1;

	if(setsockopt(soc, SOL_SOCKET, SO_REUSEADDR, (char *)&flag, sizeof(flag))<0){
		perror("[-]Could not set reuse address option on DHCP socket\n");
		return FALSE;
	}

	if(bind(soc, (struct sockaddr *)&client_sin, sizeof(client_sin))<0){
		perror("[-]Failed to bind \n");
		return FALSE;
	}

	return soc;
}


int send_dhcp_discover(int soc, u_char *client_mac){
	struct dhcp_packet dhcp;
	struct sockaddr_in server_sin;
	int bytes;
	int requested_address = 0;

	memset(&dhcp, 0, sizeof(struct dhcp_packet));
	dhcp.op_code = BOOTREQUEST;
	dhcp.htype = HTYPE_ETHER;
	dhcp.hlen = HTYPE_ETHER_LEN;
	dhcp.hop_count = 0;
	/*
	srand = 乱数の種を変更
	time = 時刻情報取得. NULLで戻り値を取得できる.
	*/
	srand(time(NULL));
	dhcp.transaction_id = htonl(random());
	dhcp.secs = 0x00;
	dhcp.flags = htons(DHCP_BROADCAST_FLAG);
	memcpy(dhcp.chaddr, client_mac, HTYPE_ETHER_LEN);

	/*Magic Cookie*/
	dhcp.options[0] = '\x63';
	dhcp.options[1] = '\x82';
	dhcp.options[2] = '\x53';
	dhcp.options[3] = '\x63';

	dhcp.options[4] = DHCP_OPTION_MESSAGE_TYPE;
	dhcp.options[5] = '\x01';
	dhcp.options[6] = DHCP_DISCOVER;

	dhcp.options[7] = DHCP_OPTION_REQUESTED_ADDRESS;
	dhcp.options[8] = '\x04';
	memcpy(&dhcp.options[9], &requested_address, sizeof(requested_address));

	memset(&server_sin, 0, sizeof(struct sockaddr_in));

	server_sin.sin_family = AF_INET;
	server_sin.sin_port = htons(DHCP_SERVER_PORT);
	server_sin.sin_addr.s_addr = INADDR_BROADCAST;

	if(bytes=sendto(soc, &dhcp, sizeof(struct dhcp_packet), 0, (struct sockaddr *)&server_sin, sizeof(server_sin))<0){
		perror("[-]Failed to send DHCP Discover packet\n");
		return FALSE;
	}

	return TRUE;
}

int recv_dhcp_offer(int soc){
	struct dhcp_packet dhcp;
	struct sockaddr_in server_sin;
	int bytes;

	memset(&server_sin, 0, sizeof(struct sockaddr_in));

	server_sin.sin_family = AF_INET;
	server_sin.sin_port = htons(DHCP_SERVER_PORT);
	server_sin.sin_addr.s_addr = inet_addr("192.168.11.1");

	if(bytes=recvfrom(soc, &dhcp, sizeof(struct dhcp_packet), 0, &server_sin, sizeof(struct  sockaddr_in)<0)){
		perror("[-]Failed to recieve DHCP Offer packet\n");
		return FALSE;
	}

	return TRUE;
}

int main(int argc, char *argv[]){
	int dhcp_soc_send, dhcp_soc_recv;
	u_char *mac;
	char *device;
	struct dhcp_packet dhcp;

	device = "rp2";

	if((dhcp_soc_send = open_dhcp_socket(device))!=FALSE) printf("[+]Opened dhcp_socket\n");
	if((dhcp_soc_recv = open_dhcp_socket_for_recv())!=FALSE) printf("[+]Opened dhcp_socket from recv\n");
	mac = HARDWARE_ADDR;
	if((send_dhcp_discover(dhcp_soc_send, mac))==TRUE) printf("[+]Sent DHCP Discover\n");
	if((recv_dhcp_offer(dhcp_soc_recv))==TRUE) printf("[+]Recieved DHCP Offer\n");

	close(dhcp_soc_send);
	close(dhcp_soc_recv);

	return 0;
}
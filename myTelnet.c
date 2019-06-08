/*
	This implementation aims to developed skills on C and usage of the socket API for TCP.
	We have kept to ensure our code enjoyable to read through comments in order
	to facilitate reuse of any part of this code quickly.

	The program allows an user to execute commands on a distant machine. 

	@Author : Abdel Saïd TARNAGDA, student at INSA Toulouse (National Applied Science Institute Toulouse)
	@Licence : MIT Licence

*/


#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <signal.h>
#include <errno.h>
#include <sys/ioctl.h>

#define READ_MAX 200
#define STR_MAX 200
#define STR_MAXX 500
#define USERNAME_MAX 50

struct telnetParams {
	char* username;
	char* machinename;
};

struct TELNET_PACKET{
	int code; // Give the result of the execution when a command was send, or verification code
	char* payload;
};

/* 
	Global parameters related to the connection 
*/

//Socket
int sock_local;
int sock_local_conn;
int ports[] = {23, 8500, 8501}; // Lists of usable port for myTelnet
int role=1; // 1 => executer (by default), 0 => server


int running = 1;
char usage[] = "usage: myTelnet -s | user_name@host_name\n";

//Sur Ubuntu
char tmp_folder_path[] = "/tmp/";
char abs_path_users_list[] = "/etc/passwd";

void printUsage(){
	printf("%s", usage);
}


void resetString(char* s){
	int i=0;
	while(s[i] != '\0'){
		s[i] = 0;
		i++;
	}
}


/*

	Fomat of the usage :

	Executer
	myTelnet user_name@server_name

	Server
	myTelnet -s

*/

int getTelnetParams(struct telnetParams* p, char* str){
	int prt_size;
	char *ptr=strtok(str, "@ ");

	prt_size = strlen(ptr);
	if(prt_size == 0){
		return -1;
	}

	p->username = malloc(prt_size);
	strcpy(p->username, ptr);

	ptr = strtok(NULL, "@ ");

	prt_size = strlen(ptr);
	if(prt_size == 0){
		return -1;
	}

	p->machinename = malloc(prt_size);
	strcpy(p->machinename, ptr);

	return 0;
}

void telnetPacketParser(char *string_packet, struct TELNET_PACKET *tp){
	char *ptr;

	ptr = strtok(string_packet, "|");
	tp->code = atoi(ptr);

	ptr = strtok(NULL, "|");
	tp->payload = ptr;
}

void close_socket(int sock_local)
{

	printf("Bye-bye ^^\n");
	if(shutdown(sock_local, 2) == -1)
	{
		perror("Fail shutdown(...) ");
	}
	if(close(sock_local) == -1)
	{
		perror("Fail close(..)");
	}
}

void quit_myTelnel(){
	if(role)
		close_socket(sock_local);
	else
		close_socket(sock_local_conn);
	
}

int authentification(char* ident){
	FILE* f;
	char line[USERNAME_MAX];
	char * ptr;
	int res = 0;
	if( (f = fopen(abs_path_users_list, "r")) == NULL){
		printf("Cannot open the users list file : < %s >\n", abs_path_users_list);
		quit_myTelnel(sock_local);	
	}

	while( fgets(line, USERNAME_MAX, f) && !res){
		ptr = strtok(line, ":");
		//printf(" <%s> is checked !\n", ptr);
		res = strcmp(ptr, ident); // strcmp returns 0 when equality others values else
		res = res ? 0 : 1; // We give 1 to res when equality and 0 else
	}

	return res;
}


int main(int argc, char **argv) {

	if(argc != 2){
		//printf error params
		printUsage();
		exit(-1);
	}
	struct telnetParams user_params;

	

	int c;
	
	extern char *optarg;
	//extern int optind;

	

	//Reading of options
	while ((c = getopt(argc, argv, "cs")) != -1) {

		switch (c) {
			case 's':
				role = 0;
				break;
			default:
				printUsage();
				break;
		}
	}


	if ((sock_local = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("Fail of socket ");
		exit(-1);
	}
	
	struct sockaddr_in addr_local, addr_com;

	/*
		Variables of sending and receiving sections
	*/
	struct TELNET_PACKET tpacket;
	char command[STR_MAX];
	char result[STR_MAXX];
	int size_received;
	
	struct sigaction action;
	
	action.sa_handler = quit_myTelnel;
	action.sa_flags=0;

	sigaction(SIGQUIT, &action, NULL);


	/*
		WHEN WE ARE AN EXECUTER
	*/
	if (role) {

		struct hostent *ht_com = malloc(sizeof(struct hostent));


		if( getTelnetParams(&user_params, argv[1]) == -1)
		{
			printUsage();
			exit(-1);
		}

		memset(&addr_com, 0, sizeof(addr_com));

		addr_com.sin_family = AF_INET;
		addr_com.sin_port = ports[0];


		if ((ht_com = gethostbyname(user_params.machinename)) == NULL) {
			perror("Fail of gethostbyname(...) ");
			exit(-1);
		}

		memcpy((char *) &(addr_com.sin_addr.s_addr), ht_com->h_addr, ht_com->h_length);

		if (connect(sock_local, (struct sockaddr *) &addr_com, sizeof(addr_com)) == -1)
		{
			printf("[ myTelnet ] : Cannot find the host < %s > \n", user_params.machinename);
			exit(-1);
		}

		strcpy(command, user_params.username);

		//Sending
		if (write(sock_local, command, strlen(command)) == -1) {
			perror("Erreur write()");
			exit(-1);
		}


		//Receive
		if ((size_received = read(sock_local, result, STR_MAXX)) < 0) {
			perror("Fail read(...) ");
			exit(1);
		}

		telnetPacketParser(result, &tpacket);

		if(tpacket.code == -1){
			
			printf("Fail of connection !\n%s\n", tpacket.payload);
			running = 0; // There are certainly an authentification problem so we leave the program
		}

		
		
		while(running)
		{

			printf("<%s@%s> : ", user_params.username, user_params.machinename);
			scanf("%s", command);

			if(strcmp(command, "exit")){
			
				//Sending
				if (write(sock_local, command, strlen(command)) == -1) {
					perror("Erreur write()");
					exit(-1);
				}


				//Receive
				if ((size_received = read(sock_local, result, STR_MAXX)) < 0) {
					perror("Fail of read(...) ");
					exit(1);
				}
			
				if(size_received > 0)
				{
					printf("%s", result);
				
					resetString(result);
					resetString(command);
			
				}

			}else
			{
				running = 0;
			}
			

		}

		/*
			Closing of the local socket of the client 
		*/
		printf("[myTelnel] Bye-bye ^^\n");
		if(shutdown(sock_local, 2) == -1)
		{
			perror("Fail shutdown(...) ");
		}
		if(close(sock_local) == -1)
		{
			perror("Fail close(..)");
		}
	
	}
	/*
		WHEN WE ARE A SERVER
	*/
	else 
	{


		int exec_status;
		int not_authen=1;

		//File descriptor of the 
		//int sock_local_conn;
		
		socklen_t size_addr = sizeof(struct sockaddr_in); // Size of the address structure 
		
		char redirection[STR_MAX];
		char tmp_files_name[2][STR_MAX];
		int size_result;

		FILE* tmp_result; // Flux of the file that contains the result of the execution 

		//Initialisation of the address
		memset(&addr_local, 0, sizeof(struct sockaddr_in));
		memset(&addr_com, 0, sizeof(struct sockaddr_in));

		
		addr_local.sin_family = AF_INET;
		addr_local.sin_port = ports[0];
		addr_local.sin_addr.s_addr = INADDR_ANY;
		

		if(bind(sock_local, (struct sockaddr*) &addr_local, size_addr) == -1)
		{
			perror("Erreur bind(...) ");
			exit(-1);
		}

		if (listen(sock_local, 10) == -1)//CONSTANTE 1
		{
			perror("Erreur listen(...) ");
			exit(-1);
		}

		do{

			if ( (sock_local_conn = accept(sock_local, (struct sockaddr *) &addr_com, &size_addr)) == -1) {
				perror("Fail of accept(...) ");
				exit(-1);
			}

			/*
				AUTHENTIFICATION of the client
			*/

			// We read the username sended by the client
			if ((size_received = read(sock_local_conn, command, STR_MAX)) < 0) {
				perror("Fail of read(...) ");
				exit(-1);
			}

			// We give to the authentification function the username
			if (authentification(command)){

				strcpy(result, "Connection success !\n*** [ myTelnet v1 ] ***\n");
				size_result = strlen(result)+1;
				//Send
				if (write(sock_local_conn, result, size_result) == -1) {
					perror("Erreur write(...) ");
					exit(-1);
				}
				not_authen = 0;
			}
			else
			{
				sprintf(result, "-1|The username < %s > does not correspond to any user of this server !", command);

				if (write(sock_local_conn, result, strlen(result)+1) == -1) {
					perror("Erreur write(...) ");
					exit(-1);
				}

				//Closing of the local socket
				if(shutdown(sock_local, 2) == -1)
				{
					perror("Error shutdown(...) ");
				}
				if(close(sock_local) == -1)
				{
					perror("Error close(..) ");
				}

			}

		}while(not_authen);



		/*
			Adjusment of the redirection.
			Redirection ensure we save the execution result on our file dedicated for that. 
		*/

		
		strcpy(tmp_files_name[0], tmp_folder_path);
		strcpy(tmp_files_name[1], tmp_folder_path);
		strcat(tmp_files_name[0], ".result_OUT.myTelnet");
		strcat(tmp_files_name[1], ".result_ERR.myTelnet");
		
		strcpy(redirection, " > ");
		strcat(redirection, tmp_files_name[0]);
		strcat(redirection, " 2> ");
		strcat(redirection, tmp_files_name[1]);


		/*
			Begining of the execution's treatment
		*/

			
		//Receive
		while(1)
		{
			// We read the command sended by the client
			if ((size_received = read(sock_local_conn, command, STR_MAX)) < 0) {
				perror("Fail of read(...) ");
				exit(-1);
			}

			if(size_received > 0)
			{
				//We concatenate the command with the rediraction string
				strcat(command, redirection);
				
				//We determine if the command has well been executed
				exec_status = system(command);
				exec_status = exec_status ? 1 : 0;
				
				if( (tmp_result = fopen(tmp_files_name[exec_status], "r")) == NULL)
				{
					printf("Cannot file: %s \n", tmp_files_name[exec_status]);
					exit(-1);
				}

				int i=0;
				while( (c = getc(tmp_result)) != EOF ) 
				{
					result[i] = c;
					i++;
				}
				result[i] = '\0';
				size_result = i+1;

				//Send
				if (write(sock_local_conn, result, size_result) == -1) {
					perror("Erreur write(...) ");
					exit(-1);
				}
				resetString(command);
			}
			
		}

		
	}

	return 0;

}

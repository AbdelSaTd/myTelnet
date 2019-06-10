/*
	This implementation aims to developed skills on C and usage of the socket API for TCP.
	This code is commented as mush as possible in order to facilitate the reuse of any part of this code quickly.

	The port 23 has been used by default.

	The program allows an user to execute commands on a remote machine. 

	Fomat of the usage :

	Client
	myTelnet user_name@server_name

	Server
	myTelnet -s

	@Author : Abdel Saïd TARNAGDA, student in Computer Engineering at INSA Toulouse (National Applied Science Institute Toulouse)
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
#define STR_MAX 100
#define STR_MAXX 200
#define STR_MAXXX 500
#define USERNAME_MAX 50

struct telnetParams {
	char* username;
	char* machinename;
};

struct TELNET_PACKET{
	int code; // Give the result of the execution when a command was send, or verification code
	int pyld_size;
	char* payload;
};

/* 
	Global parameters related to the connexion 
*/

//Sockets
int sock_local;
int sock_local_conn;

int ports[] = {23, 8500, 8501}; // Lists of usable port for myTelnet
int role=1; // 1 => executer (by default), 0 => server

char usage[] = "usage: myTelnet [-s | user_name@host_name]\n Note: Use SIGQUIT(3) to properly close the server.\n\tFor example : kill -3 pid_of_process";

//Sur Ubuntu
char tmp_folder_path[] = "/tmp/";
char abs_path_users_list[] = "/etc/passwd";

void printUsage(){
	printf("%s", usage);
}


/* 
	Specifics parameters related to a proper closing 
*/

// These variables are specific to the server but need to be put global in order to be managed by signal handlers
FILE* tmp_result[2]; // Flux of the files that contains the result of the execution ([0]=>stdout, [1]=>stderr)
int exec_status; // 0 if well executed, 1 if not
int exec_code;


void resetString(char* s){
	int i=0;
	while(s[i] != '\0'){
		s[i] = 0;
		i++;
	}
}



int getClientParams(struct telnetParams* p, char* str){
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

	ptr = strtok(string_packet, "¤");
	tp->code = atoi(ptr);

	ptr = strtok(NULL, "¤");
	tp->pyld_size = atoi(ptr);

	ptr = strtok(NULL, "¤");
	tp->payload = ptr;
}

void close_socket(int sock_local)
{
	if(shutdown(sock_local, 2) == -1)
	{
		perror("Error shutdown(...) ");
	}
	if(close(sock_local) == -1)
	{
		perror("Error close(..)");
	}
}

void quit_myTelnel(){
	printf("Bye-bye ^^\n");
	if(role)
		close_socket(sock_local);
	else
	{
		close_socket(sock_local_conn);
		close_socket(sock_local);
		if( fclose(tmp_result[exec_status]) == EOF){
			perror("Error fclose(...)");
		}
	}
	
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

	while(fgets(line, USERNAME_MAX, f) && !res){
		ptr = strtok(line, ":");
		//printf(" <%s> is checked !\n", ptr);
		res = strcmp(ptr, ident); // strcmp returns 0 when equality others values else
		res = res ? 0 : 1; // We give 1 to res when equality and 0 else
	}

	return res;
}

/*
	The function looks for the first occurence of the new-line character '\n' and replace it by end character '\0'
	return 0 if the replacement has been made, -1 else.
*/
int removeNewLineChar(char * str, int MAX_SIZE){
	int i=0;
	while(str[i] != '\n' && str[i] != '\0' && i < MAX_SIZE){
		i++;
	}

	if(str[i] == '\n'){
		str[i] = '\0';
		return 0;
	}
	return -1;
}


int putsCounter(char* str){
	int i=0;

	while(str[i] != '\0')
	{
		putchar(str[i]);
		i++;
	}

	return i;

}


/*
	DEBUG functions
*/

void printSize(char* nom_str, char *str){
	printf("( %s ) < %s > (%ld)\n", nom_str, str, strlen(str));
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
		perror("Error socket ");
		exit(-1);
	}
	
	struct sockaddr_in addr_local, addr_com;

	/*
		Variables of sending and receiving sections
	*/
	struct TELNET_PACKET tpacket;
	char command[STR_MAXX];
	char result[STR_MAXXX];
	int size_received, size_sent;
	int running_session=1;
	
	struct sigaction action;
	
	action.sa_handler = quit_myTelnel;
	action.sa_flags=0;

	sigaction(SIGQUIT, &action, NULL);


	/*
		WHEN WE ARE AN CLIENT
	*/
	if (role) {

		struct hostent *ht_com = malloc(sizeof(struct hostent));
		int char_payload_read;
		
		if( getClientParams(&user_params, argv[1]) == -1)
		{
			printUsage();
			exit(-1);
		}

		memset(&addr_com, 0, sizeof(addr_com));

		addr_com.sin_family = AF_INET;
		addr_com.sin_port = ports[0];


		if ((ht_com = gethostbyname(user_params.machinename)) == NULL) {
			perror("Error gethostbyname(...) ");
			exit(-1);
		}

		memcpy((char *) &(addr_com.sin_addr.s_addr), ht_com->h_addr, ht_com->h_length);

		if (connect(sock_local, (struct sockaddr *) &addr_com, sizeof(addr_com)) == -1)
		{
			printf("[ myTelnet ] : Cannot find the host < %s > \n", user_params.machinename);
			exit(-1);
		}

		/*
		 * AUTHENTIFICATION : Sending of the username to the server
		 */

		printf("Sending of username\n");
		strcpy(command, user_params.username);
		//Sending
		if (write(sock_local, command, strlen(command)) == -1) {
			perror("Erreur write()");
			exit(-1);
		}
		//Receiving
		if ((size_received = read(sock_local, result, STR_MAXXX)) < 0) {
			perror("Fail read(...) ");
			exit(1);
		}



		telnetPacketParser(result, &tpacket);

		if(tpacket.code == -1){
			printf("[ myTelnet ] : Error connexion !\n%s\n", tpacket.payload);
			running_session = 0; // There is certainly an authentification problem so we leave the program
		}
		else // Authentification succeed
		{
			printf("[ myTelnet ] : %s\n", tpacket.payload);
		}

		while(running_session)
		{

			printf("<%s@%s> : ", user_params.username, user_params.machinename);
			fgets(command, STR_MAXX-1, stdin);

			removeNewLineChar(command, STR_MAXX);

			//printf("Typed < %s > (strlen=%ld)\n", command, strlen(command));
			
			
			if(strlen(command) > 0)
			{
				//Sending
				if ( (size_sent=write(sock_local, command, strlen(command))) == -1) {
					perror("Erreur write()");
					exit(-1);
				}

				//printf("Sent < %s > (strlen=%ld)(size_sent=%d)\n", command, strlen(command), size_sent);

				//Receive
				if ((size_received = read(sock_local, result, STR_MAXXX)) < 0) {
					perror("Error read(...) ");
					exit(1);
				}

				//printf("result received %s\n", result);
			
				if(size_received > 0)
				{
					telnetPacketParser(result, &tpacket);

					switch (tpacket.code)
					{
					case -1: //Exit
						running_session=0;
						break;

					case 0: //Normal has been well executed
						if(tpacket.pyld_size > 0){
							char_payload_read = putsCounter(tpacket.payload); // puts while counting the number of character which is returned 

							while(char_payload_read < tpacket.pyld_size)
							{
								//Receive
								if ((size_received = read(sock_local, result, STR_MAXXX)) < 0) {
									perror("Error read(...) ");
									exit(1);
								}

								char_payload_read +=putsCounter(result);

							}
						}

						resetString(result);
						resetString(command);
						break;
					
					default://Command has failed
						printf("[ myTelnet ]: Command fails !\nErrorCode< %d >\n\n", tpacket.code);
						if(tpacket.pyld_size > 0){
							char_payload_read = putsCounter(tpacket.payload); // puts while counting the number of character which is returned 

							while(char_payload_read < tpacket.pyld_size)
							{
								//Receive
								if ((size_received = read(sock_local, result, STR_MAXXX)) < 0) {
									perror("Error read(...) ");
									exit(1);
								}

								char_payload_read +=putsCounter(result);

							}
						}


						resetString(result);
						resetString(command);
						break;
					}
				}
			}	

		}

		/*
			Closing of the local socket of the client 
		*/
		printf("[myTelnel] Bye-bye ^^\n");
		if(shutdown(sock_local, 2) == -1)
		{
			perror("Error shutdown(...) ");
		}
		if(close(sock_local) == -1)
		{
			perror("Error close(..)");
		}
	
	}
	/*
		WHEN WE ARE A SERVER
	*/
	else 
	{
		int not_authen;
		int running_session;

		//File descriptor of the 
		//int sock_local_conn;
		
		socklen_t size_addr = sizeof(struct sockaddr_in); // Size of the address structure 
		
		char redirection[STR_MAXX];
		char header[STR_MAX];
		char *result_command=NULL;
		char tmp_files_name[2][STR_MAXX];
		int size_result;
		int k;
		int header_size;

		

		//Initialisation of the address
		memset(&addr_local, 0, sizeof(struct sockaddr_in));
		memset(&addr_com, 0, sizeof(struct sockaddr_in));

		
		addr_local.sin_family = AF_INET;
		addr_local.sin_port = ports[0];
		addr_local.sin_addr.s_addr = INADDR_ANY;
		

		if(bind(sock_local, (struct sockaddr*) &addr_local, size_addr) == -1)
		{
			perror("Error bind(...) ");
			exit(-1);
		}

		if (listen(sock_local, 10) == -1)//CONSTANTE 1
		{
			perror("Error listen(...) ");
			exit(-1);
		}

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

		while(1)
		{
			running_session=1;
			not_authen=1;
			while(not_authen)
			{
				printf("Waiting for a new connexion...\n");
				// Waiting for a new connexion (session)
				if ( (sock_local_conn = accept(sock_local, (struct sockaddr *) &addr_com, &size_addr)) == -1) {
					perror("Error accept(...) ");
					exit(-1);
				}

				printf("New connexion arrived \n");

				/*
					AUTHENTIFICATION of the client
				*/

				// We read the username sent by the client
				if ((size_received = read(sock_local_conn, command, STR_MAXX)) < 0) {
					perror("Error read(...) ");
					exit(-1);
				}

				
				printf("Authentification processing...\n");
				// We give to the authentification function the username
				if(authentification(command)){
					
					printf("Connexion succeed !\n< %s > is now connected !\n", command);
					char success_msg[] = "0¤20¤Connexion success !";
					size_result = strlen(success_msg)+1; // + '\0'
					//Send
					if (write(sock_local_conn, success_msg, size_result) == -1) {
						perror("Erreur write(...) ");
						quit_myTelnel();
						exit(-1);
					}
					not_authen = 0;
					resetString(command);
				}
				else
				{

					printf("Connexion failed !\n");
					sprintf(result, "-1¤71¤The username < %s > does not correspond to any user of this server !", command);

					if (write(sock_local_conn, result, strlen(result)+1) == -1) {
						perror("Erreur write(...) ");
						exit(-1);
					}

					//Shutting and closing down of the local socket dedicated to the connecxion
					close_socket(sock_local_conn);
				}
			}

			/*
				Begining of the execution's treatment
			*/

			while(running_session)
			{
				

				// We read the command sent by the client
				if((size_received = read(sock_local_conn, command, STR_MAXX)) < 0) {
					perror("Error read(...) ");
					quit_myTelnel();
					exit(-1);
				}

				if(size_received > 0)
				{
					//printf("A session of execution is running...\n");
					printf("Command received < %s >\n", command);

					if(strcmp(command, "exit") /* && strcmp(command, "quit") */)
					{	
						//We concatenate the command with the redirection string
						strcat(command, redirection);
						
						//We determine if the command has well been executed
						exec_code = system(command);
						exec_status = exec_code ? 1 : 0;


						for(int i=0; i<2; i++)
						{
							if( (tmp_result[i] = fopen(tmp_files_name[i], "r")) == NULL)
							{
								printf("Cannot open file: %s \n", tmp_files_name[i]);
								quit_myTelnel();
								exit(-1);
							}
						}

						char dest_sprintf[STR_MAX];
					
						sprintf(dest_sprintf, "%d¤", exec_code);

						strcpy(header, dest_sprintf);
						//printf("res = %s (i=%d=strlen(res))\n", result, i);
						//printf("Res from file < ");

						
						
						k=0;
						while( (c = getc(tmp_result[exec_status])) != EOF) 
						{
							k++;
						}

						sprintf(dest_sprintf, "%d¤", k);
						strcat(header, dest_sprintf);
						header_size = strlen(header);
						size_result=header_size+k+1; // + '\0'
						printSize("header", header);
						printf("hearSize %d\n", header_size);

						if( (result_command = malloc((size_result)*sizeof(char))) == NULL){
							perror("Error malloc(...)");
							//Proper exit
							quit_myTelnel();
							exit(-1);
						}

						printf(" k=%d,  size_result=%d\n", k, size_result);
				

						if(fseek(tmp_result[exec_status], 0, SEEK_SET) != 0){
							perror("Error fseek(...)");
							//Proper exit
							quit_myTelnel();
							exit(-1);
						}

						strcpy(result_command, header);

						k=header_size;
						while( (c = getc(tmp_result[exec_status])) != EOF) 
						{
							result_command[k] = c;
							k++;
						}
						result_command[k]='\0';

						printf("Res {*** %s ***} (%d)\n", result_command, size_result);

						//Send
						if (write(sock_local_conn, result_command, size_result) == -1) {
							perror("Error write(...) ");
							quit_myTelnel();
							exit(-1);
						}

						resetString(command);
						free(result_command);

					}
					else{// We leave the program, exit has been typed

						printf("The connexion ended \n");
						strcpy(result, "-1¤22¤**Connexion closed**"); // Note that '¤' count for 2 char 
						size_result=strlen(result)+1;
						//Send
						if (write(sock_local_conn, result, size_result) == -1) {
							perror("Error write(...) ");
							quit_myTelnel();
							exit(-1);
						}

						running_session = 0;

						//Closing of the local socket
						if(shutdown(sock_local_conn, 2) == -1)
						{
							perror("Error shutdown(...) ");
						}
						if(close(sock_local_conn) == -1)
						{
							perror("Error close(...) ");
						}
					}
				}
				
			}

		}

		
	}

	return 0;

}

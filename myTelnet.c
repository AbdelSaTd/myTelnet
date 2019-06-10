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
int file_open=0; // In case of interruption allows to know if a file is currently open
int ongoing_connexion=0;

char usage[] = "usage: myTelnet [-s | user_name@host_name]\n\nNote: Use SIGQUIT(3) to properly close the server.\n\tFor example : kill -3 pid_of_process\n";

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
	if(ptr == NULL){
		return -1;
	}
	else{
		prt_size = strlen(ptr);
		if(prt_size == 0){
			return -1;
		}
	}

	p->username = malloc(prt_size);
	strcpy(p->username, ptr);

	ptr = strtok(NULL, "@ ");

	if(ptr == NULL){
		return -1;
	}
	else{
		prt_size = strlen(ptr);
		if(prt_size == 0){
			return -1;
		}
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

void quit_handler(){
	if(role==0)
	{
		if(ongoing_connexion)
			close_socket(sock_local_conn);

		if(file_open)
		{
			if( fclose(tmp_result[0]) == EOF){
				perror("Error fclose(...)");
			}
			if( fclose(tmp_result[1]) == EOF){
				perror("Error fclose(...)");
			}
		}
		printf("Server shut down ! \n");
	}

	close_socket(sock_local);
	exit(-1);
	
}

int authentification(char* ident){ // Return 1 when the username is authentified, 0 if not, and -1 when error
	FILE* f;
	char line[USERNAME_MAX];
	char * ptr;
	int res = 0;

	if( (f = fopen(abs_path_users_list, "r")) == NULL){
		printf("Cannot open the users list file : < %s >\n", abs_path_users_list);
		// Critical error; we have to close the connexion and exit the program.
		res = -1;
	}
	else
	{
		while(fgets(line, USERNAME_MAX, f) && !res){
			ptr = strtok(line, ":");
			//printf(" <%s> is checked !\n", ptr);
			res = strcmp(ptr, ident); // strcmp returns 0 when equality others values else
			res = res ? 0 : 1; // We give 1 to res when equality and 0 else
		}
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


/* 
	MAIN FUNCTION
*/

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
	
	action.sa_handler = quit_handler;
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
			if(close(sock_local) == -1)
			{
				perror("Error close(..)");
			}
			exit(-1);
		}

		memset(&addr_com, 0, sizeof(addr_com));

		addr_com.sin_family = AF_INET;
		addr_com.sin_port = ports[0];


		if ((ht_com = gethostbyname(user_params.machinename)) == NULL) {
			printf("[ myTelnet ] : Cannot find the host < %s > \n", user_params.machinename);
			if(close(sock_local) == -1)
			{
				perror("Error close(..)");
			}
			exit(-1);
		}

		

		memcpy((char *) &(addr_com.sin_addr.s_addr), ht_com->h_addr, ht_com->h_length);

		if (connect(sock_local, (struct sockaddr *) &addr_com, sizeof(addr_com)) == -1)
		{
			printf("[ myTelnet ] :  The host < %s > does not respond \n", user_params.machinename);
			if(close(sock_local) == -1)
			{
				perror("Error close(..)");
			}
			exit(-1);
		}

		

		/*
		 * AUTHENTIFICATION : Sending of the username to the server
		 */

		//printf("Sending of username\n");
		strcpy(command, user_params.username);
		//Sending
		if (write(sock_local, command, strlen(command)) == -1) {
			perror("Error write(...)");
			close_socket(sock_local);
			exit(-1);
		}
		//Receiving
		if ((size_received = read(sock_local, result, STR_MAXXX)) < 0) {
			perror("Error read(...) ");
			close_socket(sock_local);
			exit(-1);
		}



		telnetPacketParser(result, &tpacket);

		if(tpacket.code == -1){
			printf("[ myTelnet ] : Error connexion :\n%s\n", tpacket.payload);
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
					perror("Error write(...)");
					close_socket(sock_local);
					exit(-1);
				}

				//printf("Sent < %s > (strlen=%ld)(size_sent=%d)\n", command, strlen(command), size_sent);

				//Receive
				if ((size_received = read(sock_local, result, STR_MAXXX)) < 0) {
					perror("Error read(...) ");
					close_socket(sock_local);
					exit(-1);
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
									perror("Error read(...)");
									close_socket(sock_local);
									exit(-1);
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
									close_socket(sock_local);
									exit(-1);
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


		free(user_params.username);
		free(user_params.machinename);

		/*
			Closing of the local socket of the client 
		*/
		printf("[myTelnel] Bye-bye !\n");
		close_socket(sock_local);		
	
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
		int authen_code;

		

		//Initialisation of the address
		memset(&addr_local, 0, sizeof(struct sockaddr_in));
		memset(&addr_com, 0, sizeof(struct sockaddr_in));

		
		addr_local.sin_family = AF_INET;
		addr_local.sin_port = ports[0];
		addr_local.sin_addr.s_addr = INADDR_ANY;
		

		if(bind(sock_local, (struct sockaddr*) &addr_local, size_addr) == -1)
		{
			perror("Error bind(...) ");
			close_socket(sock_local);
			exit(-1);
		}

		if (listen(sock_local, 10) == -1)//CONSTANTE 1
		{
			perror("Error listen(...) ");
			close_socket(sock_local);
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
					close_socket(sock_local);
					exit(-1);
				}

				ongoing_connexion = 1;

				printf("New connexion arrived !\n");

				/*
					AUTHENTIFICATION of the client
				*/

				// We read the username sent by the client
				if ((size_received = read(sock_local_conn, command, STR_MAXX)) < 0) {
					perror("Error read(...) [username] ");
					close_socket(sock_local_conn);
				}
				else
				{

					printf("\tAuthentification processing...\n");
					// We give to the authentification function the username
					authen_code = authentification(command);
					if(authen_code==1)
					{
						
						printf("\tConnexion succeed !\n< %s > is now connected !\n", command);
						char success_msg[] = "0¤20¤Connexion success !";
						size_result = strlen(success_msg)+1; // + '\0'
						//Send
						if (write(sock_local_conn, success_msg, size_result) == -1) 
						{
							perror("Error write(...) ");
							// We close the connexion and waiting for a new one
							close_socket(sock_local_conn);
						}
						else
						{
							not_authen = 0; // The client is well authentified we can start the command's processing
						}
						
						resetString(command);
					}
					else // Authentification has failed !
					{
						if(authen_code == 0) // The username has not been found in the system
						{
							printf("Connexion failed !\n");
							sprintf(result, "-1¤71¤The username < %s > does not correspond to any user of this machine !", command);

							if (write(sock_local_conn, result, strlen(result)+1) == -1) {
								perror("Erreur write(...) ");
								//Close the connexion (below) and waiting for a new one
							}

							//Shutting and closing down of the local socket dedicated to the connexion
							close_socket(sock_local_conn);
						}
						else // case -1 : File cannot be found !
						{
							// Critical error; we have to close the connexion and exit the program.
							close_socket(sock_local);
							exit(-1);
						}
						
					}
				}
			}




			/*
				Begining of the execution's treatment
			*/

			while(running_session)
			{
				// We read the command sent by the client
				if((size_received = read(sock_local_conn, command, STR_MAXX)) < 0) {
					perror("Error read(...) [client_command]");
					//We close the connexion and waiting for a new one
					running_session=0;
					close_socket(sock_local_conn);
				}

				
				if(size_received > 0) // We have received a command we can start the processing
				{
					//printf("A session of execution is running...\n");
					printf("\t\tCommand received < %s >\n", command);

					if(strcmp(command, "exit") /* && strcmp(command, "quit") */)
					{	
						//We concatenate the command with the redirection string
						strcat(command, redirection);
						
						//We determine if the command has well been executed, and then which file should be used
						exec_code = system(command);
						exec_status = exec_code ? 1 : 0;


						/*
							From here each error that occur is a critical one.
							Because a command has been executed on the remote machine (server), but it is unable to send back the result.
							We think that an official program should at least notify the client about this situation before finish its execution (a server which has an internal problem is no longer useful). 
							Which will bring another step of verification.
							So for simplification here we decide to simply close the connexion and exit the program. 
						*/


						for(int i=0; i<2; i++)
						{
							if( (tmp_result[i] = fopen(tmp_files_name[i], "r")) == NULL)
							{
								printf("Cannot open file: %s \n", tmp_files_name[i]);
								close_socket(sock_local);
								exit(-1);
							}
						}
						file_open=1;

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
						//printSize("header", header);
						//printf("hearSize %d\n", header_size);

						if( (result_command = malloc((size_result)*sizeof(char))) == NULL){
							perror("Error malloc(...)");
							close_socket(sock_local);
							exit(-1);
						}

						//printf(" k=%d,  size_result=%d\n", k, size_result);
				

						// Move back the cursor at the beginning of the file
						if(fseek(tmp_result[exec_status], 0, SEEK_SET) != 0){
							perror("Error fseek(...)");
							//Proper exit
							close_socket(sock_local);
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

						printf("\t\tRes {*** %s ***} (%d)\n", result_command, size_result);

						//Send
						if (write(sock_local_conn, result_command, size_result) == -1) {
							perror("Error write(...) [result]");
							//Here again we can close the connexion and waiting for a new one
							running_session=0;
							close_socket(sock_local_conn);
						}

						resetString(command);
						free(result_command);
						fclose(tmp_result[0]);
						fclose(tmp_result[1]);
						file_open=0;
						

					}
					else{//Exit has been typed, we leave the program 

						printf("The connexion ended \n");
						strcpy(result, "-1¤22¤**Connexion closed**"); // Note that '¤' count for 2 char 
						size_result=strlen(result)+1;
						//Send
						if (write(sock_local_conn, result, size_result) == -1) {
							perror("Error write(...) ");
						}

						running_session = 0;
						//We close the connexion and waiting for a new one
						close_socket(sock_local_conn);
					
					}
					ongoing_connexion = 0;
				}
				
			}

		}

		
	}

	return 0;

}
